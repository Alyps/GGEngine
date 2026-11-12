#pragma once
#include "NavigationItem.h"
#include <cstdint>
#include <string>
#include <vector>

class Game;
namespace NavMeshFormat { struct Data; }
#if defined(GG_EDITOR)
class NavMeshDebugMesh;
#endif

// Game владеет системой, компоненты — Item. Detour скрыт в реализации.
// Навмеш статический: один на уровень, загружается из файла, указанного объёмами.
// Все методы вызываются только из главного потока.
class NavigationServer {
    struct Impl;
    // Владеющий указатель на dtNavMesh и dtNavMeshQuery; nullptr — навмеш не загружен.
    Impl* impl = nullptr;
    // Объёмы заимствованы у компонентов до removeVolume.
    std::vector<NavMeshVolumeItem*> volumes;
    // Разрешённый путь загруженных данных и хеш входа, с которым они построены.
    std::string loadedPath;
    std::uint64_t inputHash = 0;
    int tileCount = 0, polyCount = 0;
    std::string lastError;
    // Создаёт dtNavMesh из тайлов; при успехе заменяет текущий навмеш. data всегда опустошается.
    bool installTiles(NavMeshFormat::Data& data);
#if defined(GG_EDITOR)
    // Отладочная геометрия принадлежит системе; Game заимствован для RenderServer.
    NavMeshDebugMesh* debugMesh = nullptr;
    Game* debugGame = nullptr;
    bool debugVisible = false;
    void refreshDebugMesh();
#endif

public:
    NavigationServer() = default;
    ~NavigationServer();
    NavigationServer(const NavigationServer&) = delete;
    NavigationServer& operator=(const NavigationServer&) = delete;

    // Освобождает навмеш и отладочную геометрию. Game вызывает до удаления RenderServer.
    void Shutdown();
    // Не фатальная ошибка последней операции (отсутствует файл, неверный формат, нет пути).
    const std::string& getLastError() const { return lastError; }

    // Регистрация не передаёт владение Item. Если навмеш ещё не загружен,
    // загружаются данные по resolvedDataPath (пустой путь — объём без данных).
    bool addVolume(NavMeshVolumeItem& item, const std::string& resolvedDataPath);
    // Последний снятый объём выгружает навмеш уровня.
    void removeVolume(NavMeshVolumeItem& item);
    const std::vector<NavMeshVolumeItem*>& getVolumes() const { return volumes; }

    // Загружает файл навмеша, заменяя текущий; при ошибке прежний остаётся.
    bool Load(const std::string& resolvedPath);
    void Unload();
    bool hasNavMesh() const { return impl != nullptr; }
    const std::string& getLoadedPath() const { return loadedPath; }
    std::uint64_t getInputHash() const { return inputHash; }
    int getTileCount() const { return tileCount; }
    int getPolyCount() const { return polyCount; }

    // Запросы. Точки мировые; extents — половины размеров области поиска ближайшего полигона.
    // Путь состоит из углов (string pulling), первый — проекция from, последний — to или
    // ближайшая достижимая точка (тогда partial = true).
    bool FindPath(const DXVector3& from, const DXVector3& to, std::vector<DXVector3>& path,
        bool* partial = nullptr, const DXVector3& extents = DXVector3(2.0f, 4.0f, 2.0f));
    // Ближайшая точка навмеша.
    bool ProjectPoint(const DXVector3& point, DXVector3& result,
        const DXVector3& extents = DXVector3(2.0f, 4.0f, 2.0f));
    // Луч по поверхности навмеша от from к to. true — луч упёрся в край, hit — точка остановки.
    // false с пустым lastError — путь свободен, hit = to (в проекции на XZ).
    bool Raycast(const DXVector3& from, const DXVector3& to, DXVector3& hit,
        const DXVector3& extents = DXVector3(2.0f, 4.0f, 2.0f));
    // Случайная достижимая точка в радиусе от center.
    bool RandomPointInRadius(const DXVector3& center, float radius, DXVector3& result,
        const DXVector3& extents = DXVector3(2.0f, 4.0f, 2.0f));

#if defined(GG_EDITOR)
    // Принимает тайлы, построенные генератором; data после вызова пуст в любом случае.
    bool Adopt(NavMeshFormat::Data& data, const std::string& resolvedPath);
    // Отладочная отрисовка навмеша в оболочках редактора. game должен жить дольше системы.
    void SetDebugView(Game* game, bool visible);
    bool isDebugViewVisible() const { return debugVisible; }
#endif
};
