#pragma once
#if defined(GG_GAME)
#include "InputDevice.h"
#include <unordered_set>

// Game владеет состоянием; InputDevice только передаёт события своего окна.
class GameInput {
    friend class InputDevice;
    friend class Game;
    friend LRESULT DisplayWindow32::HandleMessage(HWND, UINT, WPARAM, LPARAM);
    std::unordered_set<Keys> held, pressed, released;
    DirectX::SimpleMath::Vector2 mousePosition = DirectX::SimpleMath::Vector2::Zero;
    DirectX::SimpleMath::Vector2 mouseDelta = DirectX::SimpleMath::Vector2::Zero;
    int wheelDelta = 0;
    bool active = false;
    bool focused = false;

    // Переходы очищаются до обработки сообщений, а не до обновления компонентов.
    void BeginFrame();
    void OnKey(Keys key, bool down);
    void OnMouseMove(const InputDevice::MouseMoveEventArgs& args);
    void SetFocused(bool value);
    void Reset();
public:
    void SetActive(bool value);
    bool IsActive() const { return active && focused; }
    bool IsKeyDown(Keys key) const { return held.count(key) != 0; }
    bool WasKeyPressed(Keys key) const { return pressed.count(key) != 0; }
    bool WasKeyReleased(Keys key) const { return released.count(key) != 0; }
    DirectX::SimpleMath::Vector2 GetMousePosition() const { return mousePosition; }
    DirectX::SimpleMath::Vector2 GetMouseDelta() const { return mouseDelta; }
    int GetMouseWheelDelta() const { return wheelDelta; }
};
#endif
