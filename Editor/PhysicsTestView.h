#pragma once
#include "core.h"
#include "StandartResources/RenderPresets.h"

// Только геометрия: штатный MeshComponent передаёт TransformNode в RenderItem.
inline MeshData<NormalVertexType> PhysicsTestBoxMesh(const DXColor& color)
{
    auto geometry = MeshPresets::CreateRectangle();
    for (auto& vertex : geometry.vertices) vertex.color = color;
    // Внешние грани box согласованы с clockwise-состоянием CreateOpaque.
    for (std::size_t i = 0; i < geometry.indices.size(); i += 3)
        std::swap(geometry.indices[i + 1], geometry.indices[i + 2]);
    return geometry;
}

inline bool ConfigurePhysicsTestCamera(Game& game, Level& level)
{
    auto* object = level.createObject();
    if (!object) return false;
    auto* camera = object->createComponent<EditorCamera>();
    auto* movement = object->createComponent<CameraMovementComponent>();
    if (!camera || !movement) return false;

    // Камера — обычный компонент: проекция не записывается в world меша.
    const DXVector3 position(8, 7, -10);
    const DXMatrix pose = DXMatrix::CreateLookAt(position, {0, 1.8f, 0}, DXVector3::UnitY).Invert();
    object->getTransform().set_position(position);
    object->getTransform().set_rotation(DXQuaternion::CreateFromRotationMatrix(pose));
    camera->bind_to_node(&object->getTransform());
    movement->bind_to_node(&object->getTransform());
    game.getRenderServer().setCamera(camera);
    return true;
}
