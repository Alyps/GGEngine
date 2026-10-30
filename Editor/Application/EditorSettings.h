#pragma once
#include "imgui.h"
#include <array>
#include <string>

// Только пользовательские назначения редактора; сцены и игровое приложение их не используют.
class EditorSettings {
public:
    enum class Action { Save, Undo, Redo, Delete, Duplicate, Move, Rotate, Scale,
        Navigate, ContextMenu, Forward, Backward, Left, Right, Down, Up, Count };
    struct Binding { ImGuiKey key = ImGuiKey_None; ImGuiKeyChord modifiers = 0; };
    static constexpr size_t ActionCount = static_cast<size_t>(Action::Count);
    EditorSettings();
    std::array<Binding, ActionCount> bindings;
    ImGuiKeyChord selectionModifier = ImGuiMod_Ctrl;
    const Binding& Get(Action action) const { return bindings[static_cast<size_t>(action)]; }
    Binding& Get(Action action) { return bindings[static_cast<size_t>(action)]; }
    static const char* Name(Action action);
    static bool AcceptsKey(Action action, ImGuiKey key);
    std::string Label(Action action) const;
    std::string SelectionLabel() const;
    std::string ViewportHints(bool navigating) const;
    bool Shortcut(Action action) const;
    bool Pressed(Action action) const;
    bool Held(Action action, ImGuiKeyChord additionalModifiers = 0) const;
    bool SelectionHeld() const;
    bool Validate(std::string& error) const;
    bool Load(std::string& error);
    bool Save(std::string& error) const;
};
