#if defined(GG_EDITOR)
#include <DebugDraw.h>
#include <DetourDebugDraw.h>
#include <DetourNavMesh.h>
#include "NavMeshDebugDraw.h"
#include "core.h"
#include "RenderPresets.h"
#include <vector>

namespace {
    // Подъём над поверхностью убирает z-fighting заливки с геометрией уровня.
    constexpr float LiftY = 0.05f;
    // Оболочки редактора масштабируются этим размером; единица сохраняет мировые координаты.
    const DXVector3 UnitShellScale = DXVector3::One;

    // Собирает примитивы duDebugDraw в два меша: треугольники и линии.
    // Шейдер оболочек принимает TexturedVertexType, поэтому цвет RGB записан в нормаль,
    // а альфа — в u; PSNavMeshMain выводит их без освещения (мировая матрица единичная).
    class Collector final : public duDebugDraw {
    public:
        MeshData<TexturedVertexType> triangles, lines;

        Collector()
        {
            triangles.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            lines.topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        }
        void depthMask(bool) override {}
        void texture(bool) override {}
        void begin(duDebugDrawPrimitives primitive, float) override { current = primitive; pending.clear(); }
        void vertex(const float* position, unsigned int color) override { add(position[0], position[1], position[2], color); }
        void vertex(float x, float y, float z, unsigned int color) override { add(x, y, z, color); }
        void vertex(const float* position, unsigned int color, const float*) override { add(position[0], position[1], position[2], color); }
        void vertex(float x, float y, float z, unsigned int color, float, float) override { add(x, y, z, color); }
        // Незавершённый примитив отбрасывается.
        void end() override { pending.clear(); }
        // Обычная поверхность (area 0) рисуется голубым; прочие области — цветами Recast.
        unsigned int areaToCol(unsigned int area) override
        {
            return area == 0 ? duRGBA(40, 170, 255, 255) : duDebugDraw::areaToCol(area);
        }

    private:
        duDebugDrawPrimitives current = DU_DRAW_TRIS;
        std::vector<TexturedVertexType::Vertex> pending;

        void add(float x, float y, float z, unsigned int color)
        {
            // duRGBA упаковывает каналы как 0xAABBGGRR.
            const float r = (color & 0xff) / 255.0f, g = ((color >> 8) & 0xff) / 255.0f;
            const float b = ((color >> 16) & 0xff) / 255.0f, a = ((color >> 24) & 0xff) / 255.0f;
            pending.push_back(TexturedVertexType::make({ x, y + LiftY, z }, { r, g, b }, { a, 0.0f }));
            const size_t required = current == DU_DRAW_LINES ? 2 : current == DU_DRAW_TRIS ? 3 : current == DU_DRAW_QUADS ? 4 : 1;
            if (pending.size() < required) return;
            // Точки (вершины полигонов) не нужны для обзора навмеша.
            if (current == DU_DRAW_POINTS) { pending.clear(); return; }
            auto& target = current == DU_DRAW_LINES ? lines : triangles;
            const auto base = static_cast<uint32_t>(target.vertices.size());
            target.vertices.insert(target.vertices.end(), pending.begin(), pending.end());
            if (current == DU_DRAW_LINES) target.indices.insert(target.indices.end(), { base, base + 1 });
            else if (current == DU_DRAW_TRIS) target.indices.insert(target.indices.end(), { base, base + 1, base + 2 });
            else target.indices.insert(target.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            pending.clear();
        }
    };

    // Запись и её ресурсы созданы CreateSimple и принадлежат NavMeshDebugMesh.
    void ReleaseItem(Game* game, RenderItem*& item)
    {
        if (!item) return;
        if (game) game->getRenderServer().removeItem(*item);
        delete item->material->Shader();
        delete item->material->State();
        delete item->material;
        delete item->mesh;
        delete item;
        item = nullptr;
    }
}

bool NavMeshDebugMesh::Build(Game& target, const dtNavMesh& mesh, std::string& error)
{
    Release();
    Collector collector;
    // Заливка полигонов детального меша и границы тайлов/полигонов.
    duDebugDrawNavMesh(&collector, mesh, 0);

    RenderServer& renderer = target.getRenderServer();
    const std::string& standard = target.getResourceManager().GetStandardResourceDirectory();
    const auto create = [&](const MeshData<TexturedVertexType>& data, RenderItem*& item) {
        // Пустой навмеш не создаёт записей, это не ошибка.
        if (data.vertices.empty()) return true;
        item = RenderItemPresets::CreateSimple(renderer.Device(), data, false, standard, true, "PSNavMeshMain");
        if (!item) return false;
        // Без узла и с единичным размером оболочки вершины остаются в мировых координатах.
        item->transformNode = nullptr;
        item->editorShellSize = &UnitShellScale;
        item->editorGame = &target;
        renderer.addItem(*item);
        return true;
    };
    game = &target;
    if (!create(collector.triangles, triangles) || !create(collector.lines, lines)) {
        Release();
        error = "Cannot create navmesh debug geometry";
        return false;
    }
    return true;
}

void NavMeshDebugMesh::Release()
{
    ReleaseItem(game, triangles);
    ReleaseItem(game, lines);
    game = nullptr;
}
#endif
