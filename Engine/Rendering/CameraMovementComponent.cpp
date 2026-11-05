#include "CameraMovementComponent.h"
#include "core.h"
#include <algorithm>
#include <cmath>

void CameraMovementComponent::Rotate(const DirectX::SimpleMath::Vector2& offset)
{
    if (offset.x == 0.0f && offset.y == 0.0f) return;

    // Мировая ориентация позволяет сохранять горизонт и у дочерней камеры.
    DXMatrix world = transformNode->worldMatrix();
    DXVector3 scale, position;
    DXQuaternion orientation;
    if (!world.Decompose(scale, orientation, position)) return;
    orientation.Normalize();

    // Углы служат только для ограничений и создания осевых кватернионов.
    const DXVector3 forward = DXVector3::Transform(DXVector3(0.0f, 0.0f, -1.0f), orientation);
    const float yaw = std::atan2(-forward.x, -forward.z) - offset.x * mouseSensitivity;
    const float pitchLimit = DirectX::XM_PIDIV2 - DirectX::XM_PI / 180.0f;
    const float pitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f))
        - offset.y * mouseSensitivity, -pitchLimit, pitchLimit);

    // SimpleMath operator* применяет первый поворот, затем второй: X, затем мировой Y.
    // Пересборка из двух осевых кватернионов исключает накопление крена.
    orientation = DXQuaternion::CreateFromAxisAngle(DXVector3::UnitX, pitch)
        * DXQuaternion::CreateFromAxisAngle(DXVector3::UnitY, yaw);

    // Возвращаем мировую ориентацию в локальную систему при наследовании поворота.
    const TransformNode* parent = transformNode->get_parent();
    if (parent && transformNode->is_inherit_rotation()) {
        DXMatrix parentWorld = parent->worldMatrix();
        DXQuaternion parentRotation;
        if (!parentWorld.Decompose(scale, parentRotation, position)) return;
        parentRotation.Normalize();
        parentRotation.Conjugate();
        orientation = orientation * parentRotation;
    }

    orientation.Normalize();
    transformNode->set_rotation(orientation);
}

void CameraMovementComponent::Update(Game& game, double deltaTime)
{
    // Команда поворота применяется один раз; разрешение навигации определяет редактор.
    const auto offset = rotationInput;
    rotationInput = DirectX::SimpleMath::Vector2::Zero;
    if (!enabled || !transformNode) return;
    const CameraComponent* camera = game.getRenderServer().getCamera();
    if (!camera || camera->getTransformNode() != transformNode) return;
    Rotate(offset);
    DXVector3 direction = movementInput;
    if (direction.LengthSquared() == 0.0f) return;

    // Нормализация сохраняет скорость при одновременном нажатии нескольких клавиш.
    direction.Normalize();
    DXQuaternion orientation = transformNode->get_rotation();
    orientation.Normalize();

    // Кватернион переводит движение в систему родителя без углов Эйлера.
    direction = DXVector3::Transform(direction, orientation);
    transformNode->move(direction * (speed * static_cast<float>(deltaTime)));
}

rapidjson::Value CameraMovementComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // enabled управляется фокусом редактора и не является настройкой ассета.
    json.AddMember("speed", speed, allocator);
    json.AddMember("mouse_sensitivity", mouseSensitivity, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* CameraMovementComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    float speedValue, sensitivity;
    if (!Float(json, "speed", speedValue, error) || !Float(json, "mouse_sensitivity", sensitivity, error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) CameraMovementComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->speed = speedValue;
    result->mouseSensitivity = sensitivity;
    return result;
}
