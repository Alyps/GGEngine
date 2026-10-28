#include "core.h"
#include "EditorCamera.h"

bool EditorCamera::getView(DXMatrix& view) const
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

DXMatrix EditorCamera::getProjection(float aspectRatio) const
{
    return DXMatrix::CreatePerspectiveFieldOfView(verticalFov, aspectRatio, nearPlane, farPlane);
}

void EditorCamera::Shutdown(Game& game)
{
    if (game.getRenderServer().getCamera() == this)
        game.getRenderServer().setCamera(nullptr);
}
