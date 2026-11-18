#include <RmlUi/Core.h>
#include <RmlUi/Core/URL.h>
#include "UIRenderer.h"
#include "UIServer.h"
#include "UIDocumentComponent.h"
#include "core.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_map>
#include <string_view>

namespace {
    namespace fs = std::filesystem;
    ResourceManager* activeResources = nullptr;
    // Глобальный FileInterface заимствует менеджер только на время вызова RmlUi.
    struct UIResourceScope {
        ResourceManager* previous = activeResources;
        explicit UIResourceScope(ResourceManager& resources) { activeResources = &resources; }
        ~UIResourceScope() { activeResources = previous; }
    };
    fs::path DiskPath(const std::string& path) { return fs::path(std::u8string(path.begin(), path.end())); }
    std::string Utf8Path(const fs::path& path) {
        const auto text = path.generic_u8string();
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    // Интерфейсы RmlUi общие для процесса; работают с абсолютными UTF-8 путями.
    class UIFileInterface final : public Rml::FileInterface {
    public:
        Rml::FileHandle Open(const Rml::String& path) override {
            FILE* file = nullptr;
            _wfopen_s(&file, DiskPath(UIServer::FilePath(path)).c_str(), L"rb");
            if (!file) Rml::Log::Message(Rml::Log::LT_ERROR, "Cannot open UI file: %s", path.c_str());
            return reinterpret_cast<Rml::FileHandle>(file);
        }
        void Close(Rml::FileHandle file) override { fclose(reinterpret_cast<FILE*>(file)); }
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
            return fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
        }
        bool Seek(Rml::FileHandle file, long offset, int origin) override {
            return _fseeki64(reinterpret_cast<FILE*>(file), offset, origin) == 0;
        }
        size_t Tell(Rml::FileHandle file) override {
            const auto position = _ftelli64(reinterpret_cast<FILE*>(file));
            return position < 0 ? 0 : static_cast<size_t>(position);
        }
    } fileInterface;

    class UISystemInterface final : public Rml::SystemInterface {
    public:
        // Заимствован только на время синхронной операции загрузки/отрисовки.
        std::string* diagnostics = nullptr;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
            OutputDebugStringA(("RmlUi: " + message + "\n").c_str());
            if (diagnostics && type <= Rml::Log::LT_WARNING && diagnostics->size() < 8192) {
                if (!diagnostics->empty()) *diagnostics += '\n';
                *diagnostics += message;
            }
            return true;
        }
        void JoinPath(Rml::String& output, const Rml::String& document, const Rml::String& path) override {
            const auto relative = DiskPath(UIServer::FilePath(path));
            const auto base = DiskPath(UIServer::FilePath(document)).parent_path();
            output = UIServer::DocumentURL(Utf8Path((relative.is_absolute() ? relative : base / relative).lexically_normal()));
        }
    } systemInterface;

    unsigned runtimeUsers = 0;
    unsigned long long nextContext = 0, nextFont = 0;
    std::unordered_map<std::string, std::string> fontFamilies;

    // LoadDocumentFromMemory сам выполняет первый layout до возврата документа.
    // Временный callback назначает шрифт раньше этого layout и не владеет документом.
    class DocumentFont final : public Rml::Plugin {
        const std::string& family;
    public:
        explicit DocumentFont(const std::string& value) : family(value) {}
        int GetEventClasses() override { return EVT_DOCUMENT; }
        void OnDocumentLoad(Rml::ElementDocument* document) override {
            document->SetProperty("font-family", family);
        }
    };

    std::string LoadFont(const std::string& path)
    {
        if (const auto found = fontFamilies.find(path); found != fontFamilies.end()) return found->second;
        const std::string family = "gg-font-" + std::to_string(++nextFont);
        if (!Rml::LoadFontFace(path, family, Rml::Style::FontStyle::Normal)) return {};
        fontFamilies.emplace(path, family);
        return family;
    }
}

std::string UIServer::DocumentURL(const std::string& path)
{
    // Непустой host сохраняет схему при нормализации URL внутри RmlUi.
    // Экранируем байты UTF-8 и спецсимволы, оставляя разделители каталогов.
    std::string result = "file://localhost/";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : path) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}

std::string UIServer::FilePath(const std::string& url)
{
    if (url.starts_with("res://") && activeResources) return activeResources->ResolvePath(url);
    // Декодируем только URL, созданные адаптером; обычные пути с '%' не меняем.
    constexpr std::string_view prefix = "file://localhost/";
    return url.starts_with(prefix) ? Rml::URL::UrlDecode(url.substr(prefix.size())) : url;
}

UIServer::~UIServer() { Shutdown(); }

struct UIServer::ClickListener final : Rml::EventListener {
    UIServer& server;
    explicit ClickListener(UIServer& owner) : server(owner) {}
    void ProcessEvent(Rml::Event& event) override {
        if (server.game.isEditorMode() || event.GetType() != "click") return;
        auto* target = event.GetTargetElement();
        auto* document = target ? target->GetOwnerDocument() : nullptr;
        // Нажатие на текст/иконку кнопки относится к ближайшему родителю с id.
        while (target && target != document && target->GetId().empty()) target = target->GetParentNode();
        if (!target || target->GetId().empty()) return;
        for (auto* component : server.components) {
            if (component->document == document && component->visible && !component->isPendingDestroy()
                && component->getOwner() && !component->getOwner()->isPendingDestroy()) {
                server.pendingClicks.push_back({ component, component->documentRevision, target->GetId() });
                break;
            }
        }
    }
};

bool UIServer::Init()
{
    if (context) return true;
    lastError.clear();
    auto* device = game.getRenderServer().Device();
    // Backend требует DX11.1; проверяем интерфейс до вызова его конструктора.
    ID3D11Device1* device1 = nullptr;
    if (!device || FAILED(device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device1)))) {
        lastError = "RmlUi requires a DX11.1 device.";
        return false;
    }
    device1->Release();
    if (!acquiredRuntime) {
        if (runtimeUsers == 0) {
            Rml::SetFileInterface(&fileInterface);
            Rml::SetSystemInterface(&systemInterface);
            if (!Rml::Initialise()) { lastError = "Cannot initialise RmlUi."; return false; }
        }
        ++runtimeUsers;
        acquiredRuntime = true;
    }
    renderer = new (std::nothrow) UIRenderer(game);
    if (!renderer) { lastError = "Cannot allocate UI renderer."; return false; }
    contextName = "GGEngine-UI-" + std::to_string(++nextContext);
    context = Rml::CreateContext(contextName, { 1, 1 }, renderer);
    if (!context) {
        Rml::ReleaseRenderManagers();
        delete renderer; renderer = nullptr;
        lastError = "Cannot create UI context.";
        return false;
    }
    clickListener = new (std::nothrow) ClickListener(*this);
    if (!clickListener) { Shutdown(); lastError = "Cannot allocate UI click listener."; return false; }
    // Capture получает событие один раз, даже когда id есть у нескольких родителей.
    context->GetRootElement()->AddEventListener("click", clickListener, true);
    return true;
}

void UIServer::Shutdown()
{
    CancelMouse();
    // Документы и GPU-кеш RmlUi освобождаются раньше адаптера и ResourceManager.
    for (auto* component : components) component->document = nullptr;
    components.clear();
    if (context) { Rml::RemoveContext(contextName); context = nullptr; }
    delete clickListener; clickListener = nullptr;
    if (acquiredRuntime) {
        Rml::ReleaseRenderManagers();
        if (--runtimeUsers == 0) { Rml::Shutdown(); fontFamilies.clear(); }
        acquiredRuntime = false;
    }
    delete renderer; renderer = nullptr;
    lastError.clear();
}

void UIServer::Add(UIDocumentComponent& component)
{
    if (std::find(components.begin(), components.end(), &component) != components.end()) return;
    components.push_back(&component);
    Load(component);
}

void UIServer::Remove(UIDocumentComponent& component)
{
    ++component.documentRevision;
    std::erase_if(pendingClicks, [&component](const auto& click) { return click.component == &component; });
    if (component.document) { component.document->Close(); component.document = nullptr; }
    std::erase(components, &component);
    // Close откладывает удаление: завершаем его, пока зависимости ещё живы.
    if (context) context->Update();
}

void UIServer::Load(UIDocumentComponent& component, bool refresh)
{
    ++component.documentRevision;
    auto& resources = game.getResourceManager();
    UIResourceScope resourceScope(resources);
    if (component.document) { component.document->Close(); component.document = nullptr; }
    if (context) context->Update();
    component.lastError.clear();
    lastError.clear();
    if (component.documentPath.empty()) return;
    // Старые абсолютные пути переводим в переносимые ссылки при привязке к Game.
    for (auto* path : { &component.documentPath, &component.fontPath }) {
        if (path->empty()) continue;
        const auto reference = resources.ResourcePath(*path);
        if (reference.empty()) { component.lastError = "UI resource must be inside res://: " + *path; return; }
        *path = reference;
    }
    if (!Init()) { component.lastError = lastError; return; }
    auto* resource = resources.load<UIDocumentResource>(component.documentPath);
    if (!resource) { component.lastError = resources.GetLastError(); return; }
    if (refresh && !resource->Reload(component.lastError)) return;
    if (refresh) Rml::Factory::ClearStyleSheetCache();

    systemInterface.diagnostics = &component.lastError;
    std::string fontFile;
    if (!component.fontPath.empty()) fontFile = resources.ResolvePath(component.fontPath);
    else {
        // Windows-шрифт даёт первый предпросмотр без копирования лицензируемых файлов в проект.
        wchar_t windowsDirectory[MAX_PATH]{};
        const UINT length = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        if (length && length < MAX_PATH) fontFile = Utf8Path(fs::path(windowsDirectory) / L"Fonts" / L"segoeui.ttf");
    }
    const std::string family = fontFile.empty() ? std::string{} : LoadFont(fontFile);
    if (family.empty()) {
        component.lastError += "\nCannot load UI font. Select a .ttf or .otf file.";
        systemInterface.diagnostics = nullptr;
        return;
    }
    DocumentFont fontSetup(family);
    Rml::RegisterPlugin(&fontSetup);
    component.document = context->LoadDocumentFromMemory(resource->GetText(), DocumentURL(resource->GetPath()));
    Rml::UnregisterPlugin(&fontSetup);
    if (component.document) {
        // Шрифт уже назначен до первого layout; теперь применяем состояние компонента.
        Sync(component);
        context->Update();
    } else if (component.lastError.empty()) component.lastError = "Cannot parse RML document.";
    systemInterface.diagnostics = nullptr;
}

void UIServer::Sync(UIDocumentComponent& component)
{
    if (!component.document) return;
    component.document->SetProperty("z-index", std::to_string(component.order));
    if (component.visible) component.document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    else component.document->Hide();
}

void UIServer::Draw(ID3D11RenderTargetView* target, int width, int height)
{
    if (!context || !renderer || !target || width <= 0 || height <= 0) return;
    UIResourceScope resourceScope(game.getResourceManager());
    const bool anyVisible = std::any_of(components.begin(), components.end(), [](const auto* component) {
        return component->document && component->visible && !component->isPendingDestroy();
    });
    if (!anyVisible) return;
    systemInterface.diagnostics = &lastError;
    context->SetDimensions({ width, height });
    context->Update();
    renderer->SetViewport(width, height);
    renderer->BeginFrame();
    context->Render();
    renderer->EndFrame(target);
    systemInterface.diagnostics = nullptr;
}

bool UIServer::ProcessMouse(int x, int y, int& buttonFlags, int& wheelDelta)
{
    if (!context || game.isEditorMode()) return false;
    UIResourceScope resourceScope(game.getResourceManager());
    const auto& viewport = game.getRenderServer().MainViewport();
    RECT client{};
    if (!GetClientRect(game.getWindow().getWindow(), &client) || client.right <= 0 || client.bottom <= 0) return false;
    // Координаты окна переводим в размер render target, в том числе после изменения окна.
    context->SetDimensions({ static_cast<int>(viewport.Width), static_cast<int>(viewport.Height) });
    context->Update();
    const int modifiers = ((GetKeyState(VK_SHIFT) & 0x8000) ? Rml::Input::KM_SHIFT : 0)
        | ((GetKeyState(VK_CONTROL) & 0x8000) ? Rml::Input::KM_CTRL : 0)
        | ((GetKeyState(VK_MENU) & 0x8000) ? Rml::Input::KM_ALT : 0);
    const bool hovered = !context->ProcessMouseMove(static_cast<int>(x * viewport.Width / client.right),
        static_cast<int>(y * viewport.Height / client.bottom), modifiers);
    bool consumed = hovered || capturedButtons != 0;
    for (int button = 0; button < 3; ++button) {
        const int down = 1 << (button * 2), up = down << 1;
        const unsigned capture = 1u << button;
        if (buttonFlags & down) {
            if (!context->ProcessMouseButtonDown(button, modifiers)) {
                capturedButtons |= capture; buttonFlags &= ~down; consumed = true;
                SetCapture(game.getWindow().getWindow());
            }
        }
        if (buttonFlags & up) {
            context->ProcessMouseButtonUp(button, modifiers);
            // Владение жестом определяется на down: отпускание вне UI тоже не попадает в игру.
            if (capturedButtons & capture) { buttonFlags &= ~up; consumed = true; }
            capturedButtons &= ~capture;
        }
    }
    if (wheelDelta && !context->ProcessMouseWheel(-static_cast<float>(wheelDelta) / WHEEL_DELTA, modifiers)) {
        wheelDelta = 0; buttonFlags &= ~RI_MOUSE_WHEEL; consumed = true;
    }
    if (!capturedButtons && GetCapture() == game.getWindow().getWindow()) ReleaseCapture();
    return consumed;
}

void UIServer::CancelMouse()
{
    // Сначала убираем hover, чтобы отмена захвата не превратилась в click.
    if (context) {
        context->ProcessMouseLeave();
        for (int button = 0; button < 3; ++button)
            if (capturedButtons & (1u << button)) context->ProcessMouseButtonUp(button, 0);
    }
    capturedButtons = 0;
    pendingClicks.clear();
    if (GetCapture() == game.getWindow().getWindow()) ReleaseCapture();
}

void UIServer::DispatchClicks()
{
    std::vector<PendingClick> clicks;
    clicks.swap(pendingClicks);
    for (const auto& click : clicks) {
        // Обработчик предыдущего события мог удалить компонент, скрыть UI или перезагрузить RML.
        if (std::find(components.begin(), components.end(), click.component) == components.end()) continue;
        auto& component = *click.component;
        if (!component.document || !component.visible || component.documentRevision != click.revision
            || component.isPendingDestroy() || !component.getOwner() || component.getOwner()->isPendingDestroy()) continue;
        game.getScriptServer().EmitNativeEvent(&component, "clicked", click.id);
    }
}
