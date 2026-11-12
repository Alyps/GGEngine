#if defined(GG_EDITOR)
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include "NavMeshBuilder.h"
#include "NavMeshFormat.h"
#include "NavMeshVolumeComponent.h"
#include "NavigationServer.h"
#include "core.h"
#include "RenderPresets.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <new>
#include <utility>
#include <vector>

namespace {
    // Вершин в полигоне Detour (не больше DT_VERTS_PER_POLYGON).
    constexpr int VertsPerPoly = 6;
    // 32-битные ссылки Detour делят 22 бита между номером тайла и номером полигона.
    constexpr int MaxTileBits = 14;

    unsigned NextPow2(unsigned v)
    {
        --v;
        v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
        return v + 1;
    }

    unsigned Ilog2(unsigned v)
    {
        unsigned result = 0;
        while (v >>= 1) ++result;
        return result;
    }

    // Объём в виде матрицы мир -> единичный куб [-0.5, 0.5] и мирового AABB для быстрых отсечений.
    struct VolumeBox {
        DXMatrix worldToUnit;
        DXVector3 min, max;
    };

    // Копия всего, что нужно рабочему потоку; заполняется только в главном потоке.
    struct BuildInput {
        std::vector<float> verts;   // x, y, z в мировых координатах
        std::vector<int> tris;      // по три индекса вершины
        std::vector<VolumeBox> volumes;
        // Боксы статических коллайдеров: внутри них проходимых мест нет (Recast считает меши полыми).
        std::vector<VolumeBox> solids;
        NavBuildSettings settings;
        bool settingsMismatch = false;
        int skippedVolumes = 0;
    };

    // FNV-1a: дешёвый и стабильный между запусками хеш.
    struct Hasher {
        std::uint64_t value = 1469598103934665603ull;
        void Bytes(const void* data, size_t size)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < size; ++i) { value ^= bytes[i]; value *= 1099511628211ull; }
        }
        template<class T> void Add(const T& item) { Bytes(&item, sizeof(T)); }
    };

    // Журнал Recast: прогресс отбрасывается, предупреждения и ошибки сохраняются для интерфейса.
    class BuildContext final : public rcContext {
    public:
        std::string text;
    protected:
        void doLog(const rcLogCategory category, const char* message, const int length) override
        {
            if (category == RC_LOG_PROGRESS) return;
            text += category == RC_LOG_ERROR ? "Error: " : "Warning: ";
            text.append(message, static_cast<size_t>((std::max)(0, length)));
            text += '\n';
        }
    };

    bool Finite(const DXVector3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    // Подвижные объекты не входят в статический навмеш: они изменят положение в игре.
    bool IsMovingObject(const GameObject& object)
    {
        for (const Component* component : object.getComponents()) {
            if (!component || component->isPendingDestroy()) continue;
            if (const auto* body = dynamic_cast<const RigidBodyComponent*>(component);
                body && body->getPhysicsItem().type != PhysicsBodyType::Static) return true;
            if (dynamic_cast<const CharacterMovementComponent*>(component)) return true;
        }
        return false;
    }

    // Снятый флаг коллайдера исключает и меши объекта: иначе объект запёкся бы по мешу.
    bool HasNavmeshExcludedCollider(const GameObject& object)
    {
        for (const Component* component : object.getComponents()) {
            if (!component || component->isPendingDestroy()) continue;
            if (const auto* collider = dynamic_cast<const BoxColliderComponent*>(component);
                collider && !collider->getColliderItem().affectsNavmesh) return true;
        }
        return false;
    }

    // Единичный куб с внешними нормалями по правилу cross(v1 - v0, v2 - v0), которое использует Recast.
    const MeshData<TexturedVertexType>& UnitCube()
    {
        static const MeshData<TexturedVertexType> cube = MeshPresets::CreateRectangle();
        return cube;
    }

    // Единичный куб, преобразованный world; false — вырожденный или нечисловой масштаб.
    bool MakeBox(const DXMatrix& world, VolumeBox& box)
    {
        const float determinant = world.Determinant();
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-9f) return false;
        box.worldToUnit = world.Invert();
        box.min = DXVector3(FLT_MAX, FLT_MAX, FLT_MAX);
        box.max = DXVector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (int corner = 0; corner < 8; ++corner) {
            const DXVector3 local((corner & 1) ? 0.5f : -0.5f, (corner & 2) ? 0.5f : -0.5f, (corner & 4) ? 0.5f : -0.5f);
            const DXVector3 point = DXVector3::Transform(local, world);
            box.min = DXVector3::Min(box.min, point);
            box.max = DXVector3::Max(box.max, point);
        }
        return Finite(box.min) && Finite(box.max);
    }

    void AddVolume(BuildInput& input, const DXMatrix& world)
    {
        // Вырожденный масштаб не задаёт объём: такой объём пропускается с предупреждением.
        VolumeBox box;
        if (MakeBox(world, box)) input.volumes.push_back(box);
        else ++input.skippedVolumes;
    }

    void AddSolid(BuildInput& input, const DXMatrix& world)
    {
        VolumeBox box;
        if (MakeBox(world, box)) input.solids.push_back(box);
    }

    void AddMesh(BuildInput& input, const MeshData<TexturedVertexType>& mesh, const DXMatrix& world)
    {
        const bool strip = mesh.topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        if (!strip && mesh.topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return;
        // Зеркальное преобразование меняет обход треугольников, а Recast по нему находит верх.
        const bool mirrored = world.Determinant() < 0.0f;
        const size_t base = input.verts.size() / 3;
        if (base + mesh.vertices.size() > static_cast<size_t>(INT_MAX / 3)) return;
        std::vector<char> valid(mesh.vertices.size(), 0);
        input.verts.reserve(input.verts.size() + mesh.vertices.size() * 3);
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            const DXVector3 point = DXVector3::Transform(DXVector3(mesh.vertices[i].position), world);
            valid[i] = Finite(point);
            input.verts.insert(input.verts.end(), { valid[i] ? point.x : 0.0f, valid[i] ? point.y : 0.0f, valid[i] ? point.z : 0.0f });
        }
        const auto addTriangle = [&](uint32_t a, uint32_t b, uint32_t c) {
            if (a >= valid.size() || b >= valid.size() || c >= valid.size()) return;
            if (!valid[a] || !valid[b] || !valid[c] || a == b || b == c || a == c) return;
            if (mirrored) std::swap(b, c);
            input.tris.insert(input.tris.end(), { static_cast<int>(base + a), static_cast<int>(base + b), static_cast<int>(base + c) });
        };
        const auto& indices = mesh.indices;
        if (!strip) {
            for (size_t i = 0; i + 2 < indices.size(); i += 3) addTriangle(indices[i], indices[i + 1], indices[i + 2]);
        } else {
            // В полосе каждый нечётный треугольник имеет обратный обход.
            for (size_t i = 0; i + 2 < indices.size(); ++i) {
                if (i & 1) addTriangle(indices[i], indices[i + 2], indices[i + 1]);
                else addTriangle(indices[i], indices[i + 1], indices[i + 2]);
            }
        }
    }

    // Общий обход для генерации и проверки устаревания: input == nullptr — только хеш.
    void VisitLevel(Level& level, BuildInput* input, Hasher& hash)
    {
        bool haveSettings = false;
        NavBuildSettings settings;
        bool mismatch = false;
        for (GameObject* object : level.getObjects()) {
            if (!object || object->isPendingDestroy()) continue;
            const bool movingObject = IsMovingObject(*object);
            const bool meshesExcluded = HasNavmeshExcludedCollider(*object);
            for (Component* component : object->getComponents()) {
                if (!component || component->isPendingDestroy()) continue;
                if (auto* volume = dynamic_cast<NavMeshVolumeComponent*>(component)) {
                    const TransformNode* node = volume->getTransformNode();
                    if (!node) continue;
                    // Мир <- единичный куб: размер объёма, затем полное преобразование узла.
                    const DXMatrix world = DXMatrix::CreateScale(volume->getSize()) * DXMatrix(node->worldMatrix());
                    hash.Add(1); hash.Add(world);
                    if (!haveSettings) { settings = volume->getSettings(); haveSettings = true; }
                    else if (!(settings == volume->getSettings())) mismatch = true;
                    if (input) AddVolume(*input, world);
                    continue;
                }
                if (movingObject) continue;
                if (auto* collider = dynamic_cast<BoxColliderComponent*>(component)) {
                    const ColliderItem& item = collider->getColliderItem();
                    const TransformNode* node = collider->getTransformNode();
                    if (!item.enabled || !item.affectsNavmesh || !node) continue;
                    const DXMatrix world = DXMatrix::CreateScale(item.size) * DXMatrix(node->worldMatrix());
                    hash.Add(2); hash.Add(world);
                    if (input) { AddMesh(*input, UnitCube(), world); AddSolid(*input, world); }
                } else if (auto* mesh = dynamic_cast<MeshComponent*>(component)) {
                    const TransformNode* node = mesh->getTransformNode();
                    if (!node || meshesExcluded) continue;
                    const auto& data = mesh->getMeshData();
                    const DXMatrix world(node->worldMatrix());
                    hash.Add(3); hash.Add(world);
                    hash.Add(data.vertices.size()); hash.Add(data.indices.size()); hash.Add(data.topology);
                    // Для проверки устаревания достаточно выборки вершин: полный обход дорог для больших мешей.
                    const size_t step = (std::max)(size_t(1), data.vertices.size() / 64);
                    for (size_t i = 0; i < data.vertices.size(); i += step) hash.Add(data.vertices[i].position);
                    if (input) AddMesh(*input, data, world);
                }
            }
        }
        hash.Add(settings);
        if (input) { input->settings = settings; input->settingsMismatch = mismatch; }
    }

    // Параметры Recast в ячейках и общие для всех тайлов величины.
    struct TileConfig {
        float cs = 0, ch = 0;
        int walkableHeight = 0, walkableClimb = 0, walkableRadius = 0, borderSize = 0, tileSize = 0;
        float tileWorld = 0, slope = 0;
        DXVector3 origin;
        float minY = 0, maxY = 0;
        int maxEdgeLength = 0, minRegionArea = 0, mergeRegionArea = 0;
        float maxError = 0, detailSampleDistance = 0, detailSampleMaxError = 0;
        int maxPolysPerTile = 0;
        float agentHeight = 0, agentRadius = 0, agentClimb = 0;
    };

    // Освобождает промежуточные структуры Recast одного тайла при любом выходе.
    struct TileScratch {
        rcHeightfield* solid = nullptr;
        rcCompactHeightfield* compact = nullptr;
        rcContourSet* contours = nullptr;
        rcPolyMesh* polyMesh = nullptr;
        rcPolyMeshDetail* detailMesh = nullptr;
        TileScratch() = default;
        TileScratch(const TileScratch&) = delete;
        TileScratch& operator=(const TileScratch&) = delete;
        ~TileScratch()
        {
            rcFreeHeightField(solid);
            rcFreeCompactHeightfield(compact);
            rcFreeContourSet(contours);
            rcFreePolyMesh(polyMesh);
            rcFreePolyMeshDetail(detailMesh);
        }
    };

    bool Inside(const VolumeBox& box, const DXVector3& point, float epsilon)
    {
        if (point.x < box.min.x || point.x > box.max.x || point.y < box.min.y ||
            point.y > box.max.y || point.z < box.min.z || point.z > box.max.z) return false;
        const DXVector3 local = DXVector3::Transform(point, box.worldToUnit);
        return std::abs(local.x) <= 0.5f + epsilon && std::abs(local.y) <= 0.5f + epsilon &&
            std::abs(local.z) <= 0.5f + epsilon;
    }

    // Оставляет проходимыми только спаны, поверхность которых лежит внутри хотя бы одного объёма
    // и не внутри статического коллайдера.
    void ClipToVolumes(rcCompactHeightfield& chf, const std::vector<VolumeBox>& volumes, const std::vector<VolumeBox>& solids)
    {
        constexpr float Epsilon = 1e-4f;
        for (int z = 0; z < chf.height; ++z) {
            for (int x = 0; x < chf.width; ++x) {
                const rcCompactCell& cell = chf.cells[x + z * chf.width];
                const float px = chf.bmin[0] + (x + 0.5f) * chf.cs;
                const float pz = chf.bmin[2] + (z + 0.5f) * chf.cs;
                const int end = static_cast<int>(cell.index + cell.count);
                for (int i = static_cast<int>(cell.index); i < end; ++i) {
                    if (chf.areas[i] == RC_NULL_AREA) continue;
                    // Нижняя граница свободного спана — поверхность, на которой стоит агент.
                    const DXVector3 point(px, chf.bmin[1] + chf.spans[i].y * chf.ch, pz);
                    bool inside = false;
                    for (const VolumeBox& volume : volumes)
                        if (Inside(volume, point, Epsilon)) { inside = true; break; }
                    // Точку поднимаем на высоту ячейки: верхняя грань коллайдера остаётся проходимой,
                    // а пол внутри него (полость между гранями) отсекается.
                    const DXVector3 probe(point.x, point.y + chf.ch, point.z);
                    for (size_t s = 0; inside && s < solids.size(); ++s)
                        if (Inside(solids[s], probe, -Epsilon)) inside = false;
                    if (!inside) chf.areas[i] = RC_NULL_AREA;
                }
            }
        }
    }

    // Строит один тайл. data == nullptr при успехе означает пустой тайл (нет проходимой поверхности).
    bool BuildTile(const BuildInput& input, const TileConfig& config, const std::vector<float>& triangleBounds,
        int tileX, int tileZ, BuildContext& context, std::vector<int>& tileTris, std::vector<unsigned char>& areas,
        unsigned char*& data, int& dataSize, int& polyCount, std::string& error)
    {
        data = nullptr; dataSize = 0; polyCount = 0;
        // Рамка borderSize вокруг тайла даёт Recast соседнюю геометрию для стыковки тайлов.
        const float border = config.borderSize * config.cs;
        const float bmin[3] = { config.origin.x + tileX * config.tileWorld - border, config.minY,
            config.origin.z + tileZ * config.tileWorld - border };
        const float bmax[3] = { config.origin.x + (tileX + 1) * config.tileWorld + border, config.maxY,
            config.origin.z + (tileZ + 1) * config.tileWorld + border };

        // Треугольники, задевающие расширенный тайл по XZ.
        tileTris.clear();
        const size_t triangleCount = input.tris.size() / 3;
        for (size_t t = 0; t < triangleCount; ++t) {
            const float* bounds = &triangleBounds[t * 4];
            if (bounds[0] > bmax[0] || bounds[2] < bmin[0] || bounds[1] > bmax[2] || bounds[3] < bmin[2]) continue;
            tileTris.insert(tileTris.end(), { input.tris[t * 3], input.tris[t * 3 + 1], input.tris[t * 3 + 2] });
        }
        if (tileTris.empty()) return true;

        TileScratch scratch;
        const int cells = config.tileSize + config.borderSize * 2;
        const int vertexCount = static_cast<int>(input.verts.size() / 3);
        const int tileTriangleCount = static_cast<int>(tileTris.size() / 3);
        scratch.solid = rcAllocHeightfield();
        if (!scratch.solid || !rcCreateHeightfield(&context, *scratch.solid, cells, cells, bmin, bmax, config.cs, config.ch)) {
            error = "Cannot allocate heightfield"; return false;
        }
        // Вокселизация: треугольник проходим, если его наклон не больше максимального.
        areas.assign(static_cast<size_t>(tileTriangleCount), 0);
        rcMarkWalkableTriangles(&context, config.slope, input.verts.data(), vertexCount,
            tileTris.data(), tileTriangleCount, areas.data());
        if (!rcRasterizeTriangles(&context, input.verts.data(), vertexCount, tileTris.data(), areas.data(),
            tileTriangleCount, *scratch.solid, config.walkableClimb)) {
            error = "Cannot rasterize triangles"; return false;
        }
        // Фильтры: ступеньки до climb, обрывы и места ниже роста агента.
        rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *scratch.solid);
        rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *scratch.solid);
        rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *scratch.solid);

        scratch.compact = rcAllocCompactHeightfield();
        if (!scratch.compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb,
            *scratch.solid, *scratch.compact)) {
            error = "Cannot build compact heightfield"; return false;
        }
        rcFreeHeightField(scratch.solid);
        scratch.solid = nullptr;
        // Сначала отсекаем всё вне объёмов, затем эрозия отодвигает край от стен и границ объёмов.
        ClipToVolumes(*scratch.compact, input.volumes, input.solids);
        if (!rcErodeWalkableArea(&context, config.walkableRadius, *scratch.compact) ||
            !rcBuildDistanceField(&context, *scratch.compact) ||
            !rcBuildRegions(&context, *scratch.compact, config.borderSize, config.minRegionArea, config.mergeRegionArea)) {
            error = "Cannot build navmesh regions"; return false;
        }
        scratch.contours = rcAllocContourSet();
        if (!scratch.contours || !rcBuildContours(&context, *scratch.compact, config.maxError,
            config.maxEdgeLength, *scratch.contours)) {
            error = "Cannot build navmesh contours"; return false;
        }
        if (scratch.contours->nconts == 0) return true;
        scratch.polyMesh = rcAllocPolyMesh();
        if (!scratch.polyMesh || !rcBuildPolyMesh(&context, *scratch.contours, VertsPerPoly, *scratch.polyMesh)) {
            error = "Cannot build navmesh polygons"; return false;
        }
        scratch.detailMesh = rcAllocPolyMeshDetail();
        if (!scratch.detailMesh || !rcBuildPolyMeshDetail(&context, *scratch.polyMesh, *scratch.compact,
            config.detailSampleDistance, config.detailSampleMaxError, *scratch.detailMesh)) {
            error = "Cannot build navmesh detail mesh"; return false;
        }
        rcPolyMesh& mesh = *scratch.polyMesh;
        if (mesh.npolys == 0) return true;
        // Индексы вершин Detour 16-битные, номер полигона ограничен битами ссылки.
        if (mesh.nverts >= 0xffff) { error = "Too many vertices in one tile; reduce tile size"; return false; }
        if (mesh.npolys > config.maxPolysPerTile) { error = "Too many polygons in one tile; reduce tile size"; return false; }
        // Одна область "земля" (0) с флагом проходимости 1: фильтр Detour пропускает ненулевые флаги.
        for (int i = 0; i < mesh.npolys; ++i) {
            mesh.areas[i] = 0;
            mesh.flags[i] = 1;
        }
        dtNavMeshCreateParams params{};
        params.verts = mesh.verts;
        params.vertCount = mesh.nverts;
        params.polys = mesh.polys;
        params.polyAreas = mesh.areas;
        params.polyFlags = mesh.flags;
        params.polyCount = mesh.npolys;
        params.nvp = mesh.nvp;
        params.detailMeshes = scratch.detailMesh->meshes;
        params.detailVerts = scratch.detailMesh->verts;
        params.detailVertsCount = scratch.detailMesh->nverts;
        params.detailTris = scratch.detailMesh->tris;
        params.detailTriCount = scratch.detailMesh->ntris;
        params.walkableHeight = config.agentHeight;
        params.walkableRadius = config.agentRadius;
        params.walkableClimb = config.agentClimb;
        params.tileX = tileX;
        params.tileY = tileZ;
        params.tileLayer = 0;
        rcVcopy(params.bmin, mesh.bmin);
        rcVcopy(params.bmax, mesh.bmax);
        params.cs = config.cs;
        params.ch = config.ch;
        params.buildBvTree = true;
        // Память тайла выделена dtAlloc и передаётся вызывающему коду.
        if (!dtCreateNavMeshData(&params, &data, &dataSize)) { error = "Cannot create Detour tile data"; return false; }
        polyCount = mesh.npolys;
        return true;
    }

    NavMeshBuilder::State BuildTiles(const BuildInput& input, std::atomic<int>& tilesDone, std::atomic<int>& tilesTotal,
        const std::atomic<bool>& cancel, BuildContext& context, NavMeshFormat::Data& output, int& polyTotal,
        std::string& error)
    {
        using State = NavMeshBuilder::State;
        const NavBuildSettings& settings = input.settings;
        // Общие границы всех объёмов задают начало сетки тайлов.
        DXVector3 boundsMin = input.volumes.front().min, boundsMax = input.volumes.front().max;
        for (const VolumeBox& volume : input.volumes) {
            boundsMin = DXVector3::Min(boundsMin, volume.min);
            boundsMax = DXVector3::Max(boundsMax, volume.max);
        }

        // Перевод метров в ячейки Recast, как в примере RecastDemo.
        TileConfig config;
        config.cs = settings.cellSize;
        config.ch = settings.cellHeight;
        config.walkableHeight = static_cast<int>(std::ceil(settings.agentHeight / config.ch));
        config.walkableClimb = static_cast<int>(std::floor(settings.agentMaxClimb / config.ch));
        config.walkableRadius = static_cast<int>(std::ceil(settings.agentRadius / config.cs));
        config.borderSize = config.walkableRadius + 3;
        config.tileSize = settings.tileSize;
        config.tileWorld = settings.tileSize * config.cs;
        config.slope = settings.agentMaxSlope;
        config.origin = boundsMin;
        // По Y берём запас: геометрия у границ объёма вокселизуется без обрезки, лишнее отсекает ClipToVolumes.
        config.minY = boundsMin.y - (config.walkableClimb + 1) * config.ch;
        config.maxY = boundsMax.y + settings.agentHeight + config.ch;
        config.maxEdgeLength = static_cast<int>(settings.edgeMaxLength / config.cs);
        config.maxError = settings.edgeMaxError;
        config.minRegionArea = static_cast<int>(settings.regionMinSize * settings.regionMinSize);
        config.mergeRegionArea = static_cast<int>(settings.regionMergeSize * settings.regionMergeSize);
        config.detailSampleDistance = settings.detailSampleDistance < 0.9f ? 0.0f : config.cs * settings.detailSampleDistance;
        config.detailSampleMaxError = config.ch * settings.detailSampleMaxError;
        config.agentHeight = settings.agentHeight;
        config.agentRadius = settings.agentRadius;
        config.agentClimb = settings.agentMaxClimb;
        if ((config.maxY - config.minY) / config.ch >= static_cast<float>(RC_SPAN_MAX_HEIGHT)) {
            error = "Volumes are too tall for this cell height; increase cell height"; return State::Failed;
        }

        // Размер сетки и распределение бит ссылки между тайлами и полигонами.
        const double tilesX = std::ceil((boundsMax.x - boundsMin.x) / config.tileWorld);
        const double tilesZ = std::ceil((boundsMax.z - boundsMin.z) / config.tileWorld);
        if (tilesX * tilesZ > static_cast<double>(1 << MaxTileBits)) {
            error = "Volumes cover too many tiles; increase tile size or cell size"; return State::Failed;
        }
        const int gridX = (std::max)(1, static_cast<int>(tilesX));
        const int gridZ = (std::max)(1, static_cast<int>(tilesZ));
        const int tileBits = (std::min)(static_cast<int>(Ilog2(NextPow2(static_cast<unsigned>(gridX * gridZ)))), MaxTileBits);
        const int polyBits = 22 - tileBits;
        config.maxPolysPerTile = 1 << polyBits;
        output.header.params.orig[0] = boundsMin.x;
        output.header.params.orig[1] = boundsMin.y;
        output.header.params.orig[2] = boundsMin.z;
        output.header.params.tileWidth = config.tileWorld;
        output.header.params.tileHeight = config.tileWorld;
        output.header.params.maxTiles = 1 << tileBits;
        output.header.params.maxPolys = config.maxPolysPerTile;

        // Строим только тайлы, которые пересекают AABB хотя бы одного объёма по XZ.
        std::vector<std::pair<int, int>> tiles;
        for (int z = 0; z < gridZ; ++z) {
            for (int x = 0; x < gridX; ++x) {
                const float minX = boundsMin.x + x * config.tileWorld, maxX = minX + config.tileWorld;
                const float minZ = boundsMin.z + z * config.tileWorld, maxZ = minZ + config.tileWorld;
                for (const VolumeBox& volume : input.volumes) {
                    if (volume.min.x > maxX || volume.max.x < minX || volume.min.z > maxZ || volume.max.z < minZ) continue;
                    tiles.emplace_back(x, z);
                    break;
                }
            }
        }
        tilesTotal.store(static_cast<int>(tiles.size()));

        // AABB треугольников по XZ считаем один раз для всех тайлов.
        const size_t triangleCount = input.tris.size() / 3;
        std::vector<float> triangleBounds(triangleCount * 4);
        for (size_t t = 0; t < triangleCount; ++t) {
            float minX = FLT_MAX, minZ = FLT_MAX, maxX = -FLT_MAX, maxZ = -FLT_MAX;
            for (int corner = 0; corner < 3; ++corner) {
                const float* vertex = &input.verts[static_cast<size_t>(input.tris[t * 3 + corner]) * 3];
                minX = (std::min)(minX, vertex[0]); maxX = (std::max)(maxX, vertex[0]);
                minZ = (std::min)(minZ, vertex[2]); maxZ = (std::max)(maxZ, vertex[2]);
            }
            triangleBounds[t * 4] = minX; triangleBounds[t * 4 + 1] = minZ;
            triangleBounds[t * 4 + 2] = maxX; triangleBounds[t * 4 + 3] = maxZ;
        }

        std::vector<int> tileTris;
        std::vector<unsigned char> areas;
        for (const auto& [tileX, tileZ] : tiles) {
            // Отмена проверяется между тайлами: незавершённый результат отбрасывается.
            if (cancel.load()) return State::Cancelled;
            unsigned char* data = nullptr;
            int dataSize = 0, polys = 0;
            if (!BuildTile(input, config, triangleBounds, tileX, tileZ, context, tileTris, areas, data, dataSize, polys, error))
                return State::Failed;
            if (data) output.tiles.push_back({ data, dataSize });
            polyTotal += polys;
            tilesDone.fetch_add(1);
        }
        output.header.tileCount = static_cast<std::int32_t>(output.tiles.size());
        return State::Succeeded;
    }
}

struct NavMeshBuilder::Job {
    // Вход, неизменный после Start.
    BuildInput input;
    std::uint64_t inputHash = 0;
    std::string path;
    // Прогресс и отмена разделены между потоками.
    std::atomic<int> tilesDone{ 0 }, tilesTotal{ 0 };
    std::atomic<bool> cancel{ false }, finished{ false };
    // Результат: главный поток читает его только после finished.
    State result = State::Failed;
    std::string message, log;
    NavMeshFormat::Data data;
};

NavMeshBuilder::~NavMeshBuilder()
{
    CancelAndJoin();
}

std::uint64_t NavMeshBuilder::ComputeInputHash(Level& level)
{
    Hasher hash;
    VisitLevel(level, nullptr, hash);
    return hash.value;
}

bool NavMeshBuilder::Start(Level& level, const std::string& resolvedPath, std::string& error)
{
    error.clear();
    if (isRunning()) { error = "Navmesh generation is already running"; return false; }
    if (resolvedPath.empty()) { error = "Navmesh file path is empty"; return false; }
    // Неприменённый результат прошлой генерации больше не нужен.
    delete job;
    job = nullptr;
    auto* next = new (std::nothrow) Job();
    if (!next) { error = "Cannot allocate navmesh build"; return false; }
    // Вся геометрия копируется здесь, в главном потоке.
    Hasher hash;
    VisitLevel(level, &next->input, hash);
    next->inputHash = hash.value;
    next->path = resolvedPath;
    if (next->input.volumes.empty()) {
        delete next;
        error = "Add a Nav mesh volume with a non-zero size to the scene first";
        return false;
    }
    if (!next->input.settings.Validate(error)) { delete next; return false; }
    job = next;
    state = State::Running;
    message = "Generating navmesh...";
    log.clear();
    worker = std::thread(&NavMeshBuilder::Run, job);
    return true;
}

void NavMeshBuilder::Run(Job* job)
{
    const auto started = std::chrono::steady_clock::now();
    BuildContext context;
    int polyCount = 0;
    std::string error;
    State result = BuildTiles(job->input, job->tilesDone, job->tilesTotal, job->cancel, context, job->data, polyCount, error);
    if (result == State::Succeeded) {
        job->data.header.inputHash = job->inputHash;
        // Файл пишется в рабочем потоке: запись больших навмешей не задерживает кадр редактора.
        if (!NavMeshFormat::Write(job->path, job->data, error)) result = State::Failed;
    }
    if (result != State::Succeeded) job->data.Release();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    char summary[256];
    if (result == State::Succeeded) {
        std::snprintf(summary, sizeof(summary), "Navmesh generated: %d tiles, %d polygons in %.2f s",
            static_cast<int>(job->data.tiles.size()), polyCount, seconds);
        job->message = summary;
        if (job->data.tiles.empty()) job->message += "\nNo walkable surface inside the volumes.";
        if (job->input.settingsMismatch) job->message += "\nVolumes have different settings; the first volume's settings were used.";
        if (job->input.skippedVolumes) job->message += "\nVolumes with zero size were skipped.";
    } else if (result == State::Cancelled) {
        job->message = "Navmesh generation cancelled";
    } else {
        job->message = "Navmesh generation failed: " + error;
    }
    job->log = std::move(context.text);
    job->result = result;
    // Публикуем результат: после этого рабочий поток к Job не обращается.
    job->finished.store(true, std::memory_order_release);
}

bool NavMeshBuilder::Poll()
{
    if (state != State::Running || !job || !job->finished.load(std::memory_order_acquire)) return false;
    if (worker.joinable()) worker.join();
    state = job->result;
    message = job->message;
    log = job->log;
    // Успешный результат ждёт Apply; остальное освобождаем сразу.
    if (state != State::Succeeded) {
        delete job;
        job = nullptr;
    }
    return true;
}

bool NavMeshBuilder::Apply(NavigationServer& server, std::string& error)
{
    error.clear();
    if (state != State::Succeeded || !job) { error = "No generated navmesh to apply"; return false; }
    const bool applied = server.Adopt(job->data, job->path);
    if (!applied) error = server.getLastError();
    delete job;
    job = nullptr;
    return applied;
}

void NavMeshBuilder::Cancel()
{
    if (job && state == State::Running) job->cancel.store(true);
}

void NavMeshBuilder::CancelAndJoin()
{
    if (worker.joinable()) {
        job->cancel.store(true);
        worker.join();
    }
    if (state == State::Running) {
        state = State::Cancelled;
        message = "Navmesh generation cancelled";
    }
    delete job;
    job = nullptr;
}

int NavMeshBuilder::getTilesDone() const
{
    return job ? job->tilesDone.load() : 0;
}

int NavMeshBuilder::getTilesTotal() const
{
    return job ? job->tilesTotal.load() : 0;
}
#endif
