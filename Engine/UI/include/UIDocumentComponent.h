#pragma once
#include "Component.h"

class UIServer;
class ScriptServer;
struct lua_State;
namespace Rml { class ElementDocument; class Element; }

// Object владеет компонентом, контекст RmlUi — экземпляром документа.
class UIDocumentComponent final : public Component {
    friend class UIServer;
    std::string documentPath, fontPath, lastError;
    bool visible = true;
    int order = 0;
    // Заимствованные адреса действуют только между Init и Shutdown.
    Game* game = nullptr;
    Rml::ElementDocument* document = nullptr;
    ScriptServer* eventServer = nullptr;
    unsigned long long documentRevision = 0;
    Rml::Element* FindElement(const std::string& id, std::string& error) const;
    void ReleaseEvents();
public:
    ~UIDocumentComponent() override;
    const char* GetSerializationType() const override { return "UIDocumentComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    bool ApplyDescription(const rapidjson::Value& json, std::string& error);

    void Init(Game& owner) override;
    void Shutdown(Game& owner) override;
    const std::string& GetDocumentPath() const { return documentPath; }
    const std::string& GetFontPath() const { return fontPath; }
    const std::string& GetLastError() const { return lastError; }
    bool IsVisible() const { return visible; }
    int GetOrder() const { return order; }
    void SetDocumentPath(std::string path);
    void SetFontPath(std::string path);
    void SetVisible(bool value);
    void SetOrder(int value);
    // Явная перезагрузка перечитывает RML и сбрасывает кеш стилей библиотеки.
    void Reload();
    bool ReloadForScript(std::string& error);
    bool HasElement(const std::string& id, std::string& error) const;
    bool SetText(const std::string& id, const std::string& text, std::string& error);
    bool SetStyle(const std::string& id, const std::string& property, const std::string& value, std::string& error);
    bool SetClass(const std::string& id, const std::string& name, bool enabled, std::string& error);
    bool SetElementVisible(const std::string& id, bool visible, std::string& error);
    void PushClicked(lua_State* state);
};
