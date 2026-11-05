#include "GameInput.h"

void GameInput::BeginFrame()
{
    pressed.clear();
    released.clear();
    mouseDelta = DirectX::SimpleMath::Vector2::Zero;
    wheelDelta = 0;
}

void GameInput::Reset()
{
    held.clear();
    mousePosition = DirectX::SimpleMath::Vector2::Zero;
    BeginFrame();
}

void GameInput::SetActive(bool value)
{
    if (active == value) return;
    active = value;
    Reset();
}

void GameInput::SetFocused(bool value)
{
    if (focused == value) return;
    focused = value;
    Reset();
}

void GameInput::OnKey(Keys key, bool down)
{
    if (!IsActive()) return;
    // Повторы удержания не создают нажатий; быстрое нажатие сохраняет оба перехода.
    if (down) {
        if (held.insert(key).second) pressed.insert(key);
    } else if (held.erase(key)) released.insert(key);
}

void GameInput::OnMouseMove(const InputDevice::MouseMoveEventArgs& args)
{
    if (!IsActive()) return;
    mousePosition = args.Position;
    // Все пакеты Raw Input между кадрами суммируются, чтение их не потребляет.
    mouseDelta += args.Offset;
    wheelDelta += args.WheelDelta;
}
