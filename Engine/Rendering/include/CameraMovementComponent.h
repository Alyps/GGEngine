#pragma once
#include "TransformableComponent.h"
#include "InputDevice.h"

class CameraMovementComponent : public TransformableComponent {
private:
    // Game и его InputDevice должны жить дольше подписанного компонента.
    Game* ownerGame = nullptr;
    DelegateHandle mouseMoveHandle;
    void OnMouseMove(const InputDevice::MouseMoveEventArgs& args);
public:
    CameraMovementComponent() = default;
    ~CameraMovementComponent() override;
    // Подписка хранит адрес компонента, поэтому копирование запрещено.
    CameraMovementComponent(const CameraMovementComponent&) = delete;
    CameraMovementComponent& operator=(const CameraMovementComponent&) = delete;
    float speed = 3.0f;
    // Редактор отключает навигацию при работе с другими панелями.
    bool enabled = true;
    float mouseSensitivity = 0.003f; // Радианы на единицу относительного движения мыши.
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
