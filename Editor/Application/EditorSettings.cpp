#include "EditorSettings.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace {
    using Action = EditorSettings::Action;
    const char* names[] = { "Save document", "Undo", "Redo", "Delete selection", "Duplicate selection",
        "Move tool", "Rotate tool", "Scale tool", "Camera navigation", "Viewport context menu",
        "Camera forward", "Camera backward", "Camera left", "Camera right", "Camera down", "Camera up" };
    const char* ids[] = { "save", "undo", "redo", "delete", "duplicate", "move", "rotate", "scale",
        "navigate", "context_menu", "camera_forward", "camera_backward", "camera_left", "camera_right", "camera_down", "camera_up" };
    std::string ModifierLabel(ImGuiKeyChord modifiers) {
        std::string text;
        if (modifiers & ImGuiMod_Ctrl) text += "Ctrl+";
        if (modifiers & ImGuiMod_Shift) text += "Shift+";
        if (modifiers & ImGuiMod_Alt) text += "Alt+";
        if (modifiers & ImGuiMod_Super) text += "Super+";
        return text;
    }
    bool SettingsPath(std::filesystem::path& path, std::string& error) {
        const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (!count) { error = "LOCALAPPDATA is unavailable."; return false; }
        std::wstring directory(count, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(), count);
        if (!length || length >= count) { error = "Cannot read LOCALAPPDATA."; return false; }
        directory.resize(length);
        path = std::filesystem::path(directory) / L"GGEngine" / L"Editor" / L"settings.json";
        return true;
    }
}

EditorSettings::EditorSettings()
{
    bindings = { Binding{ImGuiKey_S, ImGuiMod_Ctrl}, {ImGuiKey_Z, ImGuiMod_Ctrl}, {ImGuiKey_Y, ImGuiMod_Ctrl},
        {ImGuiKey_Delete}, {ImGuiKey_D, ImGuiMod_Ctrl}, {ImGuiKey_W}, {ImGuiKey_E}, {ImGuiKey_R},
        {ImGuiKey_MouseMiddle}, {ImGuiKey_MouseRight}, {ImGuiKey_W}, {ImGuiKey_S}, {ImGuiKey_A}, {ImGuiKey_D}, {ImGuiKey_Q}, {ImGuiKey_E} };
}

const char* EditorSettings::Name(Action action) { return names[static_cast<size_t>(action)]; }

bool EditorSettings::AcceptsKey(Action action, ImGuiKey key)
{
    if (action == Action::Navigate || action == Action::ContextMenu)
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
            error = std::string("Invalid binding: ") + names[i]; return false;
        }
        // Команды и навигация имеют разные контексты: W может означать Move и движение вперёд.
        for (size_t j = 0; j < i; ++j) {
            if ((i < 8) != (j < 8) || (i >= 10) != (j >= 10)) continue;
            const auto navigationModifiers = Get(Action::Navigate).modifiers;
            if (binding.key == bindings[j].key && (i < 10
                || (binding.modifiers | navigationModifiers) == (bindings[j].modifiers | navigationModifiers))) {
                if (i < 8 && binding.modifiers != bindings[j].modifiers) continue;
                error = std::string("Binding conflict: ") + names[j] + " / " + names[i]; return false;
            }
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
    const auto size = std::filesystem::file_size(path, code);
    if (code || size > 1024 * 1024) { error = "Cannot read editor settings (inaccessible or too large)."; return false; }
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!input || !input.read(bytes.data(), static_cast<std::streamsize>(size))) { error = "Cannot read editor settings."; return false; }
    rapidjson::Document json; json.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
    if (json.HasParseError() || !json.IsObject() || !json.HasMember("version") || !json["version"].IsInt()
        || json["version"].GetInt() != 1 || !json.HasMember("bindings") || !json["bindings"].IsObject()
        || !json.HasMember("selection_modifier") || !json["selection_modifier"].IsInt()) {
        error = "Unsupported or invalid editor settings."; return false;
    }
    EditorSettings candidate; candidate.selectionModifier = json["selection_modifier"].GetInt();
    const auto& entries = json["bindings"];
    for (size_t i = 0; i < ActionCount; ++i) {
        if (!entries.HasMember(ids[i])) continue; // Новые действия получают стандартное назначение.
        const auto& entry = entries[ids[i]];
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
        entries.AddMember(rapidjson::Value(ids[i], allocator), entry, allocator);
    }
    json.AddMember("bindings", entries, allocator);
    rapidjson::StringBuffer bytes; rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes); json.Accept(writer);
    // Временный файл и атомарная замена оставляют прежние настройки целыми при сбое записи.
    static unsigned counter = 0;
    auto temporary = path; temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++counter) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot create temporary editor settings."; return false; }
    DWORD written = 0;
    bool complete = WriteFile(file, bytes.GetString(), static_cast<DWORD>(bytes.GetSize()), &written, nullptr) && written == bytes.GetSize();
    if (complete) complete = FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    if (!complete || !closed || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); error = "Cannot save editor settings."; return false;
    }
    return true;
}
