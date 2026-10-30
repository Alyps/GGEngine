#pragma once
#include "TransformableComponent.h"
#include "InputDevice.h"

class CameraMovementComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "CameraMovementComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    // Game и его InputDevice должны жить дольше подписанного компонента.
    Game* ownerGame = nullptr;
    DelegateHandle mouseMoveHandle;
    void OnMouseMove(const InputDevice::MouseMoveEventArgs& args);
    // Приложение может передать направление по значению; стандартная игра использует WASD/QE.
    DXVector3 movementInput = DXVector3::Zero;
    bool externalMovementInput = false;
public:
    CameraMovementComponent() = default;
    ~CameraMovementComponent() override;
    // Подписка хранит адрес компонента, поэтому копирование запрещено.
    CameraMovementComponent(const CameraMovementComponent&) = delete;
    CameraMovementComponent& operator=(const CameraMovementComponent&) = delete;
    float speed = 3.0f;
    // Редактор отключает навигацию при работе с другими панелями.
    bool enabled = true;
    // Выбор кнопки — настройка приложения, а не сериализуемого компонента сцены.
    Keys rotationButton = Keys::RightButton;
    float mouseSensitivity = 0.003f; // Радианы на единицу относительного движения мыши.
    void setMovementInput(const DXVector3& direction) { movementInput = direction; externalMovementInput = true; }
    void useDefaultMovementInput() { externalMovementInput = false; }
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
