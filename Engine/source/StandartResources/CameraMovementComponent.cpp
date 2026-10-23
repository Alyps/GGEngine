#include "StandartResources/CameraMovementComponent.h"
#include "core.h"
#include <algorithm>
#include <cmath>

CameraMovementComponent::~CameraMovementComponent()
{
    if (ownerGame)
        ownerGame->getInputDevice().MouseMove.Remove(mouseMoveHandle);
}

void CameraMovementComponent::Init(Game& game)
{
    // Повторная инициализация не должна создавать вторую подписку.
    if (ownerGame)
        ownerGame->getInputDevice().MouseMove.Remove(mouseMoveHandle);
    ownerGame = &game;
    mouseMoveHandle = game.getInputDevice().MouseMove.AddRaw(this,
        &CameraMovementComponent::OnMouseMove);
}

void CameraMovementComponent::OnMouseMove(const InputDevice::MouseMoveEventArgs& args)
{
    if (!ownerGame || !transformNode) return;
    if (GetForegroundWindow() != ownerGame->getWindow().getWindow()) return;

    // Состояние ПКМ обновляется InputDevice до отправки события движения.
    const EditorCamera* camera = ownerGame->getRenderServer().getCamera();
    if (!camera || camera->getTransformNode() != transformNode) return;
    if (!ownerGame->getInputDevice().IsKeyDown(Keys::RightButton)) return;
    if (args.Offset.x == 0.0f && args.Offset.y == 0.0f) return;

    // Мировая ориентация позволяет сохранять горизонт и у дочерней камеры.
    DXMatrix world = transformNode->worldMatrix();
    DXVector3 scale, position;
    DXQuaternion orientation;
    if (!world.Decompose(scale, orientation, position)) return;
    orientation.Normalize();

    // Углы служат только для ограничений и создания осевых кватернионов.
    const DXVector3 forward = DXVector3::Transform(DXVector3(0.0f, 0.0f, -1.0f), orientation);
    const float yaw = std::atan2(-forward.x, -forward.z) - args.Offset.x * mouseSensitivity;
    const float pitchLimit = DirectX::XM_PIDIV2 - DirectX::XM_PI / 180.0f;
    const float pitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f))
        - args.Offset.y * mouseSensitivity, -pitchLimit, pitchLimit);

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
    // Ввод получает только активная камера в окне с фокусом.
    if (!transformNode || GetForegroundWindow() != game.getWindow().getWindow()) return;
    const EditorCamera* camera = game.getRenderServer().getCamera();
    if (!camera || camera->getTransformNode() != transformNode) return;
    InputDevice& input = game.getInputDevice();

    // W направлено вдоль -Z, Q опускает камеру, E поднимает.
    DXVector3 direction(
        float(input.IsKeyDown(Keys::D)) - float(input.IsKeyDown(Keys::A)),
        float(input.IsKeyDown(Keys::E)) - float(input.IsKeyDown(Keys::Q)),
        float(input.IsKeyDown(Keys::S)) - float(input.IsKeyDown(Keys::W)));
    if (direction.LengthSquared() == 0.0f) return;

    // Нормализация сохраняет скорость при одновременном нажатии нескольких клавиш.
    direction.Normalize();
    DXQuaternion orientation = transformNode->get_rotation();
    orientation.Normalize();

    // Кватернион переводит движение в систему родителя без углов Эйлера.
    direction = DXVector3::Transform(direction, orientation);
    transformNode->move(direction * (speed * static_cast<float>(deltaTime)));
}

void CameraMovementComponent::Shutdown(Game& game)
{
    if (ownerGame) ownerGame->getInputDevice().MouseMove.Remove(mouseMoveHandle);
    ownerGame = nullptr;
}
