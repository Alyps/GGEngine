#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>
#include "NavMeshFormat.h"
#include "core.h"
#if defined(GG_EDITOR)
#include "NavMeshDebugDraw.h"
#endif
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <random>

namespace {
    // Узлы поиска A* и максимальная длина коридора полигонов одного запроса.
    constexpr int MaxQueryNodes = 2048;
    constexpr int MaxPathPolys = 256;
    // Защита от чтения повреждённого или чужого файла целиком в память.
    constexpr long long MaxFileBytes = 1ll << 30;

    bool Finite(const DXVector3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    bool PositiveExtents(const DXVector3& v)
    {
        return Finite(v) && v.x > 0.0f && v.y > 0.0f && v.z > 0.0f;
    }

    // WinAPI получает UTF-16; ошибочный UTF-8 и встроенный NUL не допускаются.
    bool WidePath(const std::string& path, std::wstring& result)
    {
        if (path.empty() || path.find('\0') != std::string::npos ||
            path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
        const int length = static_cast<int>(path.size());
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, nullptr, 0);
        if (!count) return false;
        result.assign(count, L'\0');
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, result.data(), count) == count;
    }

    // Detour принимает обычный указатель на функцию; запросы идут только из главного потока.
    float RandomUnit()
    {
        static std::minstd_rand engine(static_cast<unsigned>(GetTickCount64()));
        // Detour ожидает значение в [0, 1).
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(engine);
    }

    template<class T>
    void Append(std::vector<unsigned char>& bytes, const T& value)
    {
        const auto* begin = reinterpret_cast<const unsigned char*>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(T));
    }
}

// ---------------------------------------------------------------- Настройки и Item

bool NavBuildSettings::Validate(std::string& error) const
{
    const float values[] = { agentRadius, agentHeight, agentMaxClimb, agentMaxSlope, cellSize, cellHeight,
        regionMinSize, regionMergeSize, edgeMaxLength, edgeMaxError, detailSampleDistance, detailSampleMaxError };
    for (float value : values)
        if (!std::isfinite(value)) { error = "Navigation settings must be finite numbers"; return false; }
    // Диапазоны совпадают с ограничениями полей инспектора.
    if (agentRadius < 0.0f || agentRadius > 50.0f) { error = "Agent radius must be in [0, 50]"; return false; }
    if (agentHeight <= 0.0f || agentHeight > 100.0f) { error = "Agent height must be in (0, 100]"; return false; }
    if (agentMaxClimb < 0.0f || agentMaxClimb > 100.0f) { error = "Agent max climb must be in [0, 100]"; return false; }
    if (agentMaxSlope < 0.0f || agentMaxSlope > 89.0f) { error = "Agent max slope must be in [0, 89] degrees"; return false; }
    if (cellSize < 0.01f || cellSize > 10.0f || cellHeight < 0.01f || cellHeight > 10.0f) {
        error = "Cell size and cell height must be in [0.01, 10]"; return false;
    }
    if (tileSize < 16 || tileSize > 1024) { error = "Tile size must be in [16, 1024] cells"; return false; }
    if (regionMinSize < 0.0f || regionMinSize > 1000.0f || regionMergeSize < 0.0f || regionMergeSize > 1000.0f) {
        error = "Region sizes must be in [0, 1000]"; return false;
    }
    if (edgeMaxLength < 0.0f || edgeMaxLength > 1000.0f) { error = "Edge max length must be in [0, 1000]"; return false; }
    if (edgeMaxError < 0.1f || edgeMaxError > 10.0f) { error = "Edge max error must be in [0.1, 10]"; return false; }
    if (detailSampleDistance < 0.0f || detailSampleDistance > 100.0f ||
        detailSampleMaxError < 0.0f || detailSampleMaxError > 100.0f) {
        error = "Detail sample settings must be in [0, 100]"; return false;
    }
    // Те же формулы, что использует генератор при переводе метров в ячейки Recast.
    const int walkableHeight = static_cast<int>(std::ceil(agentHeight / cellHeight));
    const int walkableClimb = static_cast<int>(std::floor(agentMaxClimb / cellHeight));
    const int walkableRadius = static_cast<int>(std::ceil(agentRadius / cellSize));
    if (walkableHeight < 3) { error = "Agent height must be at least three cell heights"; return false; }
    if (walkableClimb >= walkableHeight) { error = "Agent max climb must be lower than agent height"; return false; }
    // Поле расстояний Recast хранит значения в байте, а рамка тайла растёт с радиусом.
    if (walkableRadius > 100 || walkableHeight > 1000) {
        error = "Agent is too large for this cell size; increase cell size"; return false;
    }
    error.clear();
    return true;
}

NavMeshVolumeItem::~NavMeshVolumeItem()
{
    // Компонент снимает регистрацию в Shutdown; это страховка при прямом удалении.
    if (server) server->removeVolume(*this);
}

// ---------------------------------------------------------------- Файл навмеша

namespace NavMeshFormat {

bool Write(const std::string& path, const Data& data, std::string& error)
{
    error.clear();
    std::wstring wide;
    if (!WidePath(path, wide)) { error = "Invalid UTF-8 navmesh path"; return false; }
    // Файл собирается в памяти: заголовок, затем заголовок и данные каждого тайла.
    FileHeader header = data.header;
    header.magic = Magic;
    header.version = Version;
    header.tileCount = static_cast<std::int32_t>(data.tiles.size());
    std::vector<unsigned char> bytes;
    Append(bytes, header);
    for (const Tile& tile : data.tiles) {
        if (!tile.data || tile.size <= 0) { error = "Navmesh contains an empty tile"; return false; }
        TileHeader tileHeader;
        tileHeader.dataSize = tile.size;
        Append(bytes, tileHeader);
        bytes.insert(bytes.end(), tile.data, tile.data + tile.size);
    }
    if (bytes.size() > (std::numeric_limits<DWORD>::max)()) { error = "Navmesh is too large to save"; return false; }

    // Временный файл рядом с целевым заменяет его только после успешной записи.
    static std::atomic<unsigned long> sequence{ 0 };
    const std::wstring temporary = wide + L"." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "Cannot create navmesh file (Win32 " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD written = 0;
    bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size();
    if (success) success = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) success = false;
    if (!success) {
        DeleteFileW(temporary.c_str());
        error = "Cannot write navmesh; the previous file was preserved";
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), wide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temporary.c_str());
        error = "Cannot replace navmesh file (Win32 " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

bool Read(const std::string& path, Data& data, std::string& error)
{
    error.clear();
    data.Release();
    data.header = {};
    std::wstring wide;
    if (!WidePath(path, wide)) { error = "Invalid UTF-8 navmesh path"; return false; }
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        error = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
            ? "Navmesh file not found: " + path + " (generate the navmesh in the editor)"
            : "Cannot open navmesh file (Win32 " + std::to_string(code) + ")";
        return false;
    }
    // Читаем файл целиком; размер ограничен, чтобы повреждённый файл не занял всю память.
    LARGE_INTEGER size{};
    std::vector<unsigned char> bytes;
    bool success = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= MaxFileBytes;
    if (success) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        success = bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
            && read == bytes.size());
    }
    CloseHandle(file);
    if (!success) { error = "Cannot read navmesh file: " + path; return false; }

    size_t offset = 0;
    const auto take = [&](void* destination, size_t count) {
        if (bytes.size() - offset < count) return false;
        std::memcpy(destination, bytes.data() + offset, count);
        offset += count;
        return true;
    };
    FileHeader header;
    if (!take(&header, sizeof(header)) || header.magic != Magic) { error = "Not a GGEngine navmesh file: " + path; return false; }
    if (header.version != Version) { error = "Unsupported navmesh version; regenerate the navmesh"; return false; }
    const auto& params = header.params;
    if (!std::isfinite(params.orig[0]) || !std::isfinite(params.orig[1]) || !std::isfinite(params.orig[2]) ||
        !std::isfinite(params.tileWidth) || !std::isfinite(params.tileHeight) ||
        params.tileWidth <= 0.0f || params.tileHeight <= 0.0f || params.maxTiles <= 0 || params.maxPolys <= 0 ||
        header.tileCount < 0 || header.tileCount > params.maxTiles) {
        error = "Navmesh file header is damaged"; return false;
    }
    data.header = header;
    data.tiles.reserve(static_cast<size_t>(header.tileCount));
    for (std::int32_t i = 0; i < header.tileCount; ++i) {
        TileHeader tileHeader;
        if (!take(&tileHeader, sizeof(tileHeader)) || tileHeader.dataSize <= 0 ||
            bytes.size() - offset < static_cast<size_t>(tileHeader.dataSize)) {
            data.Release(); error = "Navmesh file is truncated"; return false;
        }
        // Память выделяется аллокатором Detour: тайл будет передан dtNavMesh во владение.
        auto* tile = static_cast<unsigned char*>(dtAlloc(static_cast<size_t>(tileHeader.dataSize), DT_ALLOC_PERM));
        if (!tile) { data.Release(); error = "Cannot allocate navmesh tile"; return false; }
        std::memcpy(tile, bytes.data() + offset, static_cast<size_t>(tileHeader.dataSize));
        offset += static_cast<size_t>(tileHeader.dataSize);
        data.tiles.push_back({ tile, tileHeader.dataSize });
    }
    if (offset != bytes.size()) { data.Release(); error = "Navmesh file has unexpected trailing data"; return false; }
    return true;
}

} // namespace NavMeshFormat

// ---------------------------------------------------------------- NavigationServer

struct NavigationServer::Impl {
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    // Фильтр по умолчанию пропускает все полигоны с ненулевыми флагами (генератор ставит 1).
    dtQueryFilter filter;
    ~Impl()
    {
        // Запрос ссылается на навмеш и освобождается первым.
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }
};

NavigationServer::~NavigationServer()
{
    // Game вызывает Shutdown раньше, пока жив RenderServer; здесь остаётся только память Detour.
    Shutdown();
}

void NavigationServer::Shutdown()
{
    // Компоненты уже сняли объёмы; оставшиеся ссылки просто отвязываем.
    for (NavMeshVolumeItem* volume : volumes) volume->server = nullptr;
    volumes.clear();
    Unload();
    lastError.clear();
}

bool NavigationServer::addVolume(NavMeshVolumeItem& item, const std::string& resolvedDataPath)
{
    if (item.server == this) return true;
    if (item.server) { lastError = "Navmesh volume is registered in another navigation server"; return false; }
    volumes.push_back(&item);
    item.server = this;
    // Первый объём с путём загружает навмеш уровня; ошибка загрузки не отменяет регистрацию.
    if (!impl && !resolvedDataPath.empty()) Load(resolvedDataPath);
    return true;
}

void NavigationServer::removeVolume(NavMeshVolumeItem& item)
{
    if (item.server != this) return;
    std::erase(volumes, &item);
    item.server = nullptr;
    // Без объёмов на уровне навмеш не нужен.
    if (volumes.empty()) Unload();
}

bool NavigationServer::Load(const std::string& resolvedPath)
{
    lastError.clear();
    NavMeshFormat::Data data;
    if (!NavMeshFormat::Read(resolvedPath, data, lastError)) return false;
    const std::uint64_t hash = data.header.inputHash;
    if (!installTiles(data)) return false;
    loadedPath = resolvedPath;
    inputHash = hash;
    return true;
}

void NavigationServer::Unload()
{
    delete impl;
    impl = nullptr;
    loadedPath.clear();
    inputHash = 0;
    tileCount = polyCount = 0;
#if defined(GG_EDITOR)
    refreshDebugMesh();
#endif
}

bool NavigationServer::installTiles(NavMeshFormat::Data& data)
{
    // Новый навмеш собирается рядом с текущим: при ошибке прежний остаётся рабочим.
    auto* next = new (std::nothrow) Impl();
    if (!next) { data.Release(); lastError = "Cannot allocate navmesh"; return false; }
    next->mesh = dtAllocNavMesh();
    next->query = dtAllocNavMeshQuery();
    if (!next->mesh || !next->query || dtStatusFailed(next->mesh->init(&data.header.params))) {
        delete next; data.Release(); lastError = "Cannot initialize navmesh"; return false;
    }
    int tiles = 0, polys = 0;
    for (NavMeshFormat::Tile& tile : data.tiles) {
        dtTileRef ref = 0;
        // При успехе dtNavMesh становится владельцем данных тайла (DT_TILE_FREE_DATA).
        if (dtStatusFailed(next->mesh->addTile(tile.data, tile.size, DT_TILE_FREE_DATA, 0, &ref))) {
            delete next; data.Release();
            lastError = "Navmesh tile is damaged or does not match the tile grid; regenerate the navmesh";
            return false;
        }
        tile.data = nullptr;
        if (const dtMeshTile* added = next->mesh->getTileByRef(ref); added && added->header)
            polys += added->header->polyCount;
        ++tiles;
    }
    data.Release();
    if (dtStatusFailed(next->query->init(next->mesh, MaxQueryNodes))) {
        delete next; lastError = "Cannot initialize navmesh query"; return false;
    }
    delete impl;
    impl = next;
    tileCount = tiles;
    polyCount = polys;
#if defined(GG_EDITOR)
    refreshDebugMesh();
#endif
    return true;
}

bool NavigationServer::FindPath(const DXVector3& from, const DXVector3& to, std::vector<DXVector3>& path,
    bool* partial, const DXVector3& extents)
{
    path.clear();
    if (partial) *partial = false;
    lastError.clear();
    if (!impl) { lastError = "Navmesh is not loaded"; return false; }
    if (!Finite(from) || !Finite(to) || !PositiveExtents(extents)) { lastError = "Invalid path query arguments"; return false; }
    // Концы пути проецируются на ближайшие полигоны внутри extents.
    dtPolyRef startRef = 0, endRef = 0;
    float start[3]{}, end[3]{};
    impl->query->findNearestPoly(&from.x, &extents.x, &impl->filter, &startRef, start);
    impl->query->findNearestPoly(&to.x, &extents.x, &impl->filter, &endRef, end);
    if (!startRef || !endRef) { lastError = "Start or end point is too far from the navmesh"; return false; }

    // Коридор полигонов A*, затем углы пути по коридору.
    dtPolyRef polys[MaxPathPolys];
    int polyCount = 0;
    const dtStatus status = impl->query->findPath(startRef, endRef, start, end, &impl->filter,
        polys, &polyCount, MaxPathPolys);
    if (dtStatusFailed(status) || polyCount <= 0) { lastError = "Path not found"; return false; }
    // Недостижимая цель: путь ведёт к ближайшей точке последнего полигона коридора.
    const bool reached = polys[polyCount - 1] == endRef;
    float target[3] = { end[0], end[1], end[2] };
    if (!reached) impl->query->closestPointOnPoly(polys[polyCount - 1], end, target, nullptr);
    float corners[MaxPathPolys * 3];
    int cornerCount = 0;
    if (dtStatusFailed(impl->query->findStraightPath(start, target, polys, polyCount, corners, nullptr, nullptr,
        &cornerCount, MaxPathPolys)) || cornerCount <= 0) {
        lastError = "Cannot build path corners"; return false;
    }
    path.reserve(static_cast<size_t>(cornerCount));
    for (int i = 0; i < cornerCount; ++i) path.emplace_back(corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]);
    if (partial) *partial = !reached || dtStatusDetail(status, DT_PARTIAL_RESULT);
    return true;
}

bool NavigationServer::ProjectPoint(const DXVector3& point, DXVector3& result, const DXVector3& extents)
{
    lastError.clear();
    if (!impl) { lastError = "Navmesh is not loaded"; return false; }
    if (!Finite(point) || !PositiveExtents(extents)) { lastError = "Invalid point query arguments"; return false; }
    dtPolyRef ref = 0;
    float nearest[3]{};
    impl->query->findNearestPoly(&point.x, &extents.x, &impl->filter, &ref, nearest);
    if (!ref) { lastError = "Point is too far from the navmesh"; return false; }
    result = DXVector3(nearest[0], nearest[1], nearest[2]);
    return true;
}

bool NavigationServer::Raycast(const DXVector3& from, const DXVector3& to, DXVector3& hit, const DXVector3& extents)
{
    lastError.clear();
    if (!impl) { lastError = "Navmesh is not loaded"; return false; }
    if (!Finite(from) || !Finite(to) || !PositiveExtents(extents)) { lastError = "Invalid raycast arguments"; return false; }
    dtPolyRef startRef = 0;
    float start[3]{};
    impl->query->findNearestPoly(&from.x, &extents.x, &impl->filter, &startRef, start);
    if (!startRef) { lastError = "Ray start is too far from the navmesh"; return false; }
    // Detour ведёт луч по полигонам в плоскости XZ; t > 1 означает, что край не встречен.
    float t = 0.0f, normal[3]{};
    dtPolyRef polys[MaxPathPolys];
    int polyCount = 0;
    if (dtStatusFailed(impl->query->raycast(startRef, start, &to.x, &impl->filter, &t, normal,
        polys, &polyCount, MaxPathPolys))) {
        lastError = "Navmesh raycast failed"; return false;
    }
    if (t > 1.0f) { hit = to; return false; }
    hit = DXVector3(start[0], start[1], start[2]) + (to - DXVector3(start[0], start[1], start[2])) * t;
    // Высоту точки остановки берём с последнего пройденного полигона.
    float height = 0.0f;
    if (polyCount > 0 && dtStatusSucceed(impl->query->getPolyHeight(polys[polyCount - 1], &hit.x, &height)))
        hit.y = height;
    return true;
}

bool NavigationServer::RandomPointInRadius(const DXVector3& center, float radius, DXVector3& result,
    const DXVector3& extents)
{
    lastError.clear();
    if (!impl) { lastError = "Navmesh is not loaded"; return false; }
    if (!Finite(center) || !std::isfinite(radius) || radius <= 0.0f || !PositiveExtents(extents)) {
        lastError = "Invalid random point arguments"; return false;
    }
    dtPolyRef startRef = 0, randomRef = 0;
    float start[3]{}, point[3]{};
    impl->query->findNearestPoly(&center.x, &extents.x, &impl->filter, &startRef, start);
    if (!startRef) { lastError = "Center is too far from the navmesh"; return false; }
    if (dtStatusFailed(impl->query->findRandomPointAroundCircle(startRef, start, radius, &impl->filter,
        &RandomUnit, &randomRef, point)) || !randomRef) {
        lastError = "Cannot find a random navmesh point"; return false;
    }
    result = DXVector3(point[0], point[1], point[2]);
    return true;
}

#if defined(GG_EDITOR)
bool NavigationServer::Adopt(NavMeshFormat::Data& data, const std::string& resolvedPath)
{
    lastError.clear();
    const std::uint64_t hash = data.header.inputHash;
    if (!installTiles(data)) return false;
    loadedPath = resolvedPath;
    inputHash = hash;
    return true;
}

void NavigationServer::SetDebugView(Game* game, bool visible)
{
    debugGame = game;
    debugVisible = visible && game;
    refreshDebugMesh();
}

void NavigationServer::refreshDebugMesh()
{
    // Прежняя геометрия всегда снимается: навмеш заменён, выгружен или скрыт.
    if (debugMesh) debugMesh->Release();
    if (!debugVisible || !debugGame || !impl || !impl->mesh) {
        delete debugMesh;
        debugMesh = nullptr;
        return;
    }
    if (!debugMesh) debugMesh = new (std::nothrow) NavMeshDebugMesh();
    if (!debugMesh) { lastError = "Cannot allocate navmesh debug view"; return; }
    std::string error;
    if (!debugMesh->Build(*debugGame, *impl->mesh, error)) lastError = error;
}
#endif
