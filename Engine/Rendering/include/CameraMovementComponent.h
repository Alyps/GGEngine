#pragma once
#if defined(GG_EDITOR)
#include "TransformableComponent.h"

class CameraMovementComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "CameraMovementComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    void Rotate(const DirectX::SimpleMath::Vector2& offset);
    // Только редактор определяет команды навигации; компонент применяет трансформацию.
    DXVector3 movementInput = DXVector3::Zero;
    DirectX::SimpleMath::Vector2 rotationInput = DirectX::SimpleMath::Vector2::Zero;
public:
    CameraMovementComponent() = default;
    CameraMovementComponent(const CameraMovementComponent&) = delete;
    CameraMovementComponent& operator=(const CameraMovementComponent&) = delete;
    float speed = 3.0f;
    // Редактор отключает навигацию при работе с другими панелями.
    bool enabled = true;
    float mouseSensitivity = 0.003f; // Радианы на единицу относительного движения мыши.
    void setMovementInput(const DXVector3& direction) { movementInput = direction; }
    void setRotationInput(const DirectX::SimpleMath::Vector2& offset) { rotationInput = offset; }
    void Init(Game& game) override {}
    void Update(Game& game, double deltaTime) override;
};
#endif
