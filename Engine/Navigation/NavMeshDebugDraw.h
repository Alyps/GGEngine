#pragma once
// Внутренний класс редактора: отладочная геометрия навмеша для RenderServer.
#if defined(GG_EDITOR)
#include <string>

class dtNavMesh;
class Game;
struct RenderItem;

// Владеет двумя RenderItem (заливка полигонов и линии границ) со всеми их GPU-ресурсами.
// Записи рисуются в проходе оболочек редактора, поэтому видны только в режиме редактирования.
class NavMeshDebugMesh {
public:
    NavMeshDebugMesh() = default;
    // Перед удалением нужно вызвать Release, пока жив RenderServer.
    ~NavMeshDebugMesh() = default;
    NavMeshDebugMesh(const NavMeshDebugMesh&) = delete;
    NavMeshDebugMesh& operator=(const NavMeshDebugMesh&) = delete;

    // Перестраивает геометрию по навмешу и регистрирует её в RenderServer игры.
    bool Build(Game& game, const dtNavMesh& mesh, std::string& error);
    // Снимает регистрацию и освобождает ресурсы; повторный вызов безопасен.
    void Release();

private:
    RenderItem* triangles = nullptr;
    RenderItem* lines = nullptr;
    // Игра, в RenderServer которой зарегистрированы записи.
    Game* game = nullptr;
};
#endif
