#include "EditorSettings.h"
#include "EditorFileUtils.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <filesystem>
#include <windows.h>

namespace {
    using Action = EditorSettings::Action;
    enum class BindingContext { Command, ViewportMouse, CameraMovement };
    struct ActionDescription {
        const char* id;
        const char* name;
        EditorSettings::Binding binding;
        BindingContext context;
    };
    // Описание действия задаёт его стабильный ключ JSON, подпись, назначение и контекст конфликтов.
    constexpr ActionDescription actions[] = {
        { "save", "Save document", {ImGuiKey_S, ImGuiMod_Ctrl}, BindingContext::Command },
        { "undo", "Undo", {ImGuiKey_Z, ImGuiMod_Ctrl}, BindingContext::Command },
        { "redo", "Redo", {ImGuiKey_Y, ImGuiMod_Ctrl}, BindingContext::Command },
        { "delete", "Delete selection", {ImGuiKey_Delete}, BindingContext::Command },
        { "duplicate", "Duplicate selection", {ImGuiKey_D, ImGuiMod_Ctrl}, BindingContext::Command },
        { "move", "Move tool", {ImGuiKey_W}, BindingContext::Command },
        { "rotate", "Rotate tool", {ImGuiKey_E}, BindingContext::Command },
        { "scale", "Scale tool", {ImGuiKey_R}, BindingContext::Command },
        { "navigate", "Camera navigation", {ImGuiKey_MouseMiddle}, BindingContext::ViewportMouse },
        { "context_menu", "Viewport context menu", {ImGuiKey_MouseRight}, BindingContext::ViewportMouse },
        { "camera_forward", "Camera forward", {ImGuiKey_W}, BindingContext::CameraMovement },
        { "camera_backward", "Camera backward", {ImGuiKey_S}, BindingContext::CameraMovement },
        { "camera_left", "Camera left", {ImGuiKey_A}, BindingContext::CameraMovement },
        { "camera_right", "Camera right", {ImGuiKey_D}, BindingContext::CameraMovement },
        { "camera_down", "Camera down", {ImGuiKey_Q}, BindingContext::CameraMovement },
        { "camera_up", "Camera up", {ImGuiKey_E}, BindingContext::CameraMovement }
    };
    static_assert(std::size(actions) == EditorSettings::ActionCount);
    std::string ModifierLabel(ImGuiKeyChord modifiers) {
        std::string text;
        if (modifiers & ImGuiMod_Ctrl) text += "Ctrl+";
        if (modifiers & ImGuiMod_Shift) text += "Shift+";
        if (modifiers & ImGuiMod_Alt) text += "Alt+";
        if (modifiers & ImGuiMod_Super) text += "Super+";
        return text;
    }
    bool SettingsPath(std::filesystem::path& path, std::string& error) {
        if (!EditorFiles::UserDirectory(path, error)) return false;
        path /= L"settings.json";
        return true;
    }
}

EditorSettings::EditorSettings()
{
    for (size_t i = 0; i < ActionCount; ++i) bindings[i] = actions[i].binding;
}

const char* EditorSettings::Name(Action action) { return actions[static_cast<size_t>(action)].name; }

bool EditorSettings::AcceptsKey(Action action, ImGuiKey key)
{
    if (actions[static_cast<size_t>(action)].context == BindingContext::ViewportMouse)
        return key == ImGuiKey_MouseMiddle || key == ImGuiKey_MouseRight;
    // Escape остаётся отменой диалога, а самостоятельные модификаторы не являются командой.
    return key >= ImGuiKey_Tab && key <= ImGuiKey_AppForward && key != ImGuiKey_Escape
        && !(key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper);
}

std::string EditorSettings::Label(Action action) const
{
    const auto& binding = Get(action);
    const char* key = binding.key == ImGuiKey_MouseMiddle ? "MMB" : binding.key == ImGuiKey_MouseRight ? "RMB" : ImGui::GetKeyName(binding.key);
    return ModifierLabel(binding.modifiers) + key;
}
std::string EditorSettings::SelectionLabel() const { return ModifierLabel(selectionModifier) + "LMB"; }

std::string EditorSettings::ViewportHints(bool navigating) const
{
    if (navigating) return Label(Action::Navigate) + ": look | " + Label(Action::Forward) + "/" + Label(Action::Backward)
        + ": forward/back | " + Label(Action::Left) + "/" + Label(Action::Right) + ": left/right | "
        + Label(Action::Down) + "/" + Label(Action::Up) + ": down/up";
    return "LMB: select | " + SelectionLabel() + ": toggle | " + Label(Action::Navigate)
        + ": hold for camera | " + Label(Action::ContextMenu) + ": menu";
}

bool EditorSettings::Shortcut(Action action) const
{
    const auto& binding = Get(action);
    return ImGui::Shortcut(binding.key | binding.modifiers, ImGuiInputFlags_RouteGlobal);
}
bool EditorSettings::Pressed(Action action) const
{
    const auto& binding = Get(action);
    const bool modifiers = action == Action::Navigate ? (ImGui::GetIO().KeyMods & binding.modifiers) == binding.modifiers
        : ImGui::GetIO().KeyMods == binding.modifiers;
    return modifiers && ImGui::IsKeyPressed(binding.key, false);
}
bool EditorSettings::Held(Action action, ImGuiKeyChord additionalModifiers) const
{
    const auto& binding = Get(action);
    const auto modifiers = binding.modifiers | additionalModifiers;
    // При навигации дополнительные модификаторы могут входить в сочетания движения камеры.
    const bool matches = action == Action::Navigate ? (ImGui::GetIO().KeyMods & modifiers) == modifiers
        : ImGui::GetIO().KeyMods == modifiers;
    return matches && ImGui::IsKeyDown(binding.key);
}
bool EditorSettings::SelectionHeld() const { return (ImGui::GetIO().KeyMods & selectionModifier) != 0; }

bool EditorSettings::Validate(std::string& error) const
{
    error.clear();
    if (selectionModifier != ImGuiMod_Ctrl && selectionModifier != ImGuiMod_Shift && selectionModifier != ImGuiMod_Alt) {
        error = "Choose Ctrl, Shift or Alt for multi-selection."; return false;
    }
    for (size_t i = 0; i < ActionCount; ++i) {
        const auto& binding = bindings[i];
        if (!AcceptsKey(static_cast<Action>(i), binding.key) || (binding.modifiers & ~ImGuiMod_Mask_)) {
            error = std::string("Invalid binding: ") + actions[i].name; return false;
        }
        // Совпадения допустимы между контекстами; внутри контекста назначение однозначно.
        const auto context = actions[i].context;
        const auto navigationModifiers = context == BindingContext::CameraMovement ? Get(Action::Navigate).modifiers : 0;
        for (size_t j = 0; j < i; ++j) {
            if (context != actions[j].context || binding.key != bindings[j].key) continue;
            if (context != BindingContext::ViewportMouse
                && (binding.modifiers | navigationModifiers) != (bindings[j].modifiers | navigationModifiers)) continue;
            error = std::string("Binding conflict: ") + actions[j].name + " / " + actions[i].name;
            return false;
        }
    }
    if (!(Get(Action::Save).modifiers & (ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super))) {
        error = "Save requires Ctrl, Alt or Super so it remains available while typing."; return false;
    }
    return true;
}

bool EditorSettings::Load(std::string& error)
{
    error.clear(); std::filesystem::path path;
    if (!SettingsPath(path, error)) return false;
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) { error = "Cannot access editor settings."; return false; }
    if (!exists) return true;
    rapidjson::Document json;
    if (!EditorFiles::ReadJson(path, json, error)) return false;
    if (!json.HasMember("version") || !json["version"].IsInt()
        || json["version"].GetInt() != 1 || !json.HasMember("bindings") || !json["bindings"].IsObject()
        || !json.HasMember("selection_modifier") || !json["selection_modifier"].IsInt()) {
        error = "Unsupported or invalid editor settings."; return false;
    }
    EditorSettings candidate; candidate.selectionModifier = json["selection_modifier"].GetInt();
    const auto& entries = json["bindings"];
    for (size_t i = 0; i < ActionCount; ++i) {
        if (!entries.HasMember(actions[i].id)) continue; // Новые действия получают стандартное назначение.
        const auto& entry = entries[actions[i].id];
        if (!entry.IsObject() || !entry.HasMember("key") || !entry["key"].IsString()
            || !entry.HasMember("modifiers") || !entry["modifiers"].IsInt()) { error = "Invalid shortcut description."; return false; }
        candidate.bindings[i] = {};
        const std::string name(entry["key"].GetString(), entry["key"].GetStringLength());
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
            if (AcceptsKey(static_cast<Action>(i), static_cast<ImGuiKey>(key)) && name == ImGui::GetKeyName(static_cast<ImGuiKey>(key)))
                candidate.bindings[i].key = static_cast<ImGuiKey>(key);
        candidate.bindings[i].modifiers = entry["modifiers"].GetInt();
    }
    if (!candidate.Validate(error)) return false;
    *this = candidate;
    return true;
}

bool EditorSettings::Save(std::string& error) const
{
    if (!Validate(error)) return false;
    std::filesystem::path path;
    if (!SettingsPath(path, error)) return false;
    std::error_code code; std::filesystem::create_directories(path.parent_path(), code);
    if (code) { error = "Cannot create the editor settings directory."; return false; }
    rapidjson::Document json; json.SetObject(); auto& allocator = json.GetAllocator();
    json.AddMember("version", 1, allocator); json.AddMember("selection_modifier", selectionModifier, allocator);
    rapidjson::Value entries(rapidjson::kObjectType);
    for (size_t i = 0; i < ActionCount; ++i) {
        rapidjson::Value entry(rapidjson::kObjectType);
        entry.AddMember("key", rapidjson::Value(ImGui::GetKeyName(bindings[i].key), allocator), allocator);
        entry.AddMember("modifiers", bindings[i].modifiers, allocator);
        entries.AddMember(rapidjson::Value(actions[i].id, allocator), entry, allocator);
    }
    json.AddMember("bindings", entries, allocator);
    rapidjson::StringBuffer bytes; rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes); json.Accept(writer);
    return EditorFiles::WriteJson(path, bytes, true, error);
}
