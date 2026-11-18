#pragma once
#include <string>
#include <vector>

class Game;
class UIRenderer;
class UIDocumentComponent;
struct ID3D11RenderTargetView;
namespace Rml { class Context; }

// Game владеет сервером; регистрации компонентов и документы не переживают его.
class UIServer {
    Game& game;
    Rml::Context* context = nullptr;
    UIRenderer* renderer = nullptr;
    std::vector<UIDocumentComponent*> components;
    std::string contextName, lastError;
    bool acquiredRuntime = false;
    struct ClickListener;
    ClickListener* clickListener = nullptr; // Сервер владеет; удаляет после контекста RmlUi.
    struct PendingClick { UIDocumentComponent* component; unsigned long long revision; std::string id; };
    std::vector<PendingClick> pendingClicks;
    unsigned capturedButtons = 0;
    bool Init();
public:
    // Внутренний URL RmlUi не сохраняется в ресурсах или компонентах.
    static std::string DocumentURL(const std::string& path);
    static std::string FilePath(const std::string& url);
    explicit UIServer(Game& owner) : game(owner) {}
    ~UIServer();
    UIServer(const UIServer&) = delete;
    UIServer& operator=(const UIServer&) = delete;
    void Shutdown();
    void Add(UIDocumentComponent& component);
    void Remove(UIDocumentComponent& component);
    void Load(UIDocumentComponent& component, bool refresh = false);
    void Sync(UIDocumentComponent& component);
    void Draw(ID3D11RenderTargetView* target, int width, int height);
    // Удаляет из пакета нажатия/колесо, забранные UI, до обработки игровым вводом.
    bool ProcessMouse(int x, int y, int& buttonFlags, int& wheelDelta);
    void CancelMouse();
    bool HasMouseCapture() const { return capturedButtons != 0; }
    void DispatchClicks();
    const std::string& GetLastError() const { return lastError; }
};
