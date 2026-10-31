#include "core.h"
#include "EditorCamera.h"

bool CameraComponent::getView(DXMatrix& view) const
{
    if (!transformNode) return false;
    DXMatrix world = transformNode->worldMatrix();

    // Из мировой трансформации получаем кватернион; масштаб камере не нужен.
    DXVector3 scale, position;
    DXQuaternion orientation;
    if (!world.Decompose(scale, orientation, position)) return false;
    orientation.Normalize();

    // В правой системе координат камера смотрит вдоль локальной оси -Z.
    const DXMatrix pose = DXMatrix::CreateFromQuaternion(orientation)
        * DXMatrix::CreateTranslation(position);
    view = pose.Invert();
    return true;
}

DXMatrix CameraComponent::getProjection(float aspectRatio) const
{
    return DXMatrix::CreatePerspectiveFieldOfView(verticalFov, aspectRatio, nearPlane, farPlane);
}

void CameraComponent::Shutdown(Game& game)
{
    if (game.getRenderServer().getCamera() == this)
        game.getRenderServer().setCamera(nullptr);
}

rapidjson::Value CameraComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    json.AddMember("vertical_fov", verticalFov, allocator);
    json.AddMember("near_plane", nearPlane, allocator);
    json.AddMember("far_plane", farPlane, allocator);
    return Serialization::Finish(std::move(json), error);
}

template<class Camera>
Component* CameraComponent::ReadCamera(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    float fov, nearValue, farValue;
    if (!Float(json, "vertical_fov", fov, error) || !Float(json, "near_plane", nearValue, error) ||
        !Float(json, "far_plane", farValue, error)) return nullptr;
    if (fov <= 0 || fov >= DirectX::XM_PI || nearValue <= 0 || farValue <= nearValue) {
        error = "Invalid camera projection parameters"; return nullptr;
    }
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) Camera();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->verticalFov = fov;
    result->nearPlane = nearValue;
    result->farPlane = farValue;
    return result;
}

Component* EditorCamera::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    return ReadCamera<EditorCamera>(json, error);
}

Component* GameCamera::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    return ReadCamera<GameCamera>(json, error);
}

GameCamera* GameCamera::Find(Level& level)
{
    for (GameObject* object : level.getObjects()) {
        if (object->isPendingDestroy()) continue;
        for (Component* component : object->getComponents()) {
            auto* candidate = dynamic_cast<GameCamera*>(component);
            if (candidate && !candidate->isPendingDestroy() && candidate->getTransformNode() &&
                !candidate->getTransformNode()->isPendingDestroy()) {
                return candidate;
            }
        }
    }
    return nullptr;
}
