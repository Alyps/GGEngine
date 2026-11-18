#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include "UIDocumentComponent.h"
#include "UIServer.h"
#include "core.h"

UIDocumentComponent::~UIDocumentComponent() { ReleaseEvents(); }

void UIDocumentComponent::ReleaseEvents()
{
    if (eventServer) eventServer->RemoveNativeEvents(this);
    eventServer = nullptr;
}

Rml::Element* UIDocumentComponent::FindElement(const std::string& id, std::string& error) const
{
    error.clear();
    if (!game || !document || isPendingDestroy() || !getOwner() || getOwner()->isPendingDestroy()) {
        error = "UI document is not available."; return nullptr;
    }
    if (id.empty() || id.find('\0') != std::string::npos) { error = "Element id must not be empty or contain NUL."; return nullptr; }
    auto* element = document->GetElementById(id);
    if (!element) error = "UI element not found: " + id;
    return element;
}

bool UIDocumentComponent::HasElement(const std::string& id, std::string& error) const
{
    return FindElement(id, error) != nullptr;
}

bool UIDocumentComponent::SetText(const std::string& id, const std::string& text, std::string& error)
{
    auto* element = FindElement(id, error);
    if (!element) return false;
    if (text.find('\0') != std::string::npos) { error = "UI text cannot contain NUL."; return false; }
    // Экранирование не позволяет строке из игры превратиться в RML-разметку.
    element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    return true;
}

bool UIDocumentComponent::SetStyle(const std::string& id, const std::string& property, const std::string& value, std::string& error)
{
    auto* element = FindElement(id, error);
    if (!element) return false;
    if (property.empty() || property.find('\0') != std::string::npos || value.find('\0') != std::string::npos
        || !element->SetProperty(property, value)) {
        error = "Invalid UI style property or value: " + property; return false;
    }
    return true;
}

bool UIDocumentComponent::SetClass(const std::string& id, const std::string& name, bool enabled, std::string& error)
{
    auto* element = FindElement(id, error);
    if (!element) return false;
    if (name.empty() || name.find_first_of(" \t\r\n\f") != std::string::npos || name.find('\0') != std::string::npos) {
        error = "Expected a single non-empty UI class name."; return false;
    }
    element->SetClass(name, enabled);
    return true;
}

bool UIDocumentComponent::SetElementVisible(const std::string& id, bool value, std::string& error)
{
    // visibility сохраняет место элемента в layout; для полного удаления используйте display.
    return SetStyle(id, "visibility", value ? "visible" : "hidden", error);
}

bool UIDocumentComponent::ReloadForScript(std::string& error)
{
    if (!game || isPendingDestroy() || !getOwner() || getOwner()->isPendingDestroy()) {
        error = "UI component is not active."; return false;
    }
    Reload();
    error = lastError;
    if (!document && error.empty()) error = "UI document path is empty.";
    return document != nullptr && error.empty();
}

void UIDocumentComponent::Init(Game& owner)
{
    game = &owner;
    game->getUIServer().Add(*this);
}

void UIDocumentComponent::Shutdown(Game& owner)
{
    ReleaseEvents();
    owner.getUIServer().Remove(*this);
    game = nullptr;
}

void UIDocumentComponent::SetDocumentPath(std::string path)
{
    if (!path.empty()) {
        const auto reference = ResourcePath(path);
        if (reference.empty()) { lastError = "UI document must be inside res://."; return; }
        path = reference;
    }
    if (path == documentPath) return;
    documentPath = std::move(path);
    if (game) game->getUIServer().Load(*this);
}

void UIDocumentComponent::SetFontPath(std::string path)
{
    if (!path.empty()) {
        const auto reference = ResourcePath(path);
        if (reference.empty()) { lastError = "UI font must be inside res://."; return; }
        path = reference;
    }
    if (path == fontPath) return;
    fontPath = std::move(path);
    if (game) game->getUIServer().Load(*this);
}

void UIDocumentComponent::SetVisible(bool value)
{
    visible = value;
    if (game) game->getUIServer().Sync(*this);
}

void UIDocumentComponent::SetOrder(int value)
{
    order = value;
    if (game) game->getUIServer().Sync(*this);
}

void UIDocumentComponent::Reload()
{
    if (game) game->getUIServer().Load(*this, true);
}

rapidjson::Value UIDocumentComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    const auto documentReference = ResourcePath(documentPath), fontReference = ResourcePath(fontPath);
    if ((!documentPath.empty() && documentReference.empty()) || (!fontPath.empty() && fontReference.empty())) {
        error = "UI resource paths must be inside res://."; return {};
    }
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // Как у остальных компонентов, сохраняем ссылки от корня ресурсов проекта.
    json.AddMember("document", Serialization::String(documentReference, allocator), allocator);
    json.AddMember("font", Serialization::String(fontReference, allocator), allocator);
    json.AddMember("visible", visible, allocator);
    json.AddMember("order", order, allocator);
    return Serialization::Finish(std::move(json), error);
}

bool UIDocumentComponent::ApplyDescription(const rapidjson::Value& json, std::string& error)
{
    std::string nextPath, nextFont;
    bool nextVisible;
    if (!Deserialization::String(json, "document", nextPath, error)
        || !Deserialization::String(json, "font", nextFont, error)
        || !Deserialization::Bool(json, "visible", nextVisible, error)) return false;
    const auto* nextOrder = Deserialization::Field(json, "order", error);
    if (!nextOrder || !nextOrder->IsInt()) { error = "UI order must be an integer."; return false; }
    // При десериализации без владельца старые пути сохраняются до Init.
    if (game) for (auto* path : { &nextPath, &nextFont }) {
        if (path->empty()) continue;
        const auto reference = ResourcePath(*path);
        if (reference.empty()) { error = "UI resource paths must be inside res://."; return false; }
        *path = reference;
    }
    const bool reload = nextPath != documentPath || nextFont != fontPath;
    documentPath = std::move(nextPath); fontPath = std::move(nextFont);
    visible = nextVisible; order = nextOrder->GetInt();
    if (game) {
        if (reload) game->getUIServer().Load(*this);
        else game->getUIServer().Sync(*this);
    }
    error.clear();
    return true;
}

Component* UIDocumentComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    // До передачи объекту вызывающий код владеет новым компонентом.
    auto* result = new (std::nothrow) UIDocumentComponent();
    if (!result) { error = "Cannot allocate UI component."; return nullptr; }
    if (!result->ApplyDescription(json, error)) { delete result; return nullptr; }
    return result;
}
