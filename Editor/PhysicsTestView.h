#pragma once
#include "core.h"
#include "StandartResources/RenderPresets.h"
#include <algorithm>

inline DXMatrix PhysicsTestViewProjection(float aspect)
{
    return DXMatrix::CreateLookAt({8, 7, -10}, {0, 1.8f, 0}, DXVector3::UnitY)
        * DXMatrix::CreatePerspectiveFieldOfView(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
}

// Scene-local adapter for the renderer's identity view/projection contract.
// Physical TransformNode stays in world space; only the submitted matrix is projected.
class PhysicsTestBoxComponent : public MeshComponent {
    DXColor tint;
    DXMatrix viewProjection = DXMatrix::Identity;
public:
    explicit PhysicsTestBoxComponent(const DXColor& color) : tint(color) {}
    void SetViewProjection(const DXMatrix& matrix) { viewProjection = matrix; }

    void SyncTransform() override
    {
        if (renderItem && transformNode)
            renderItem->world = DXMatrix(transformNode->worldMatrix()) * viewProjection;
    }

    void initRenderItem(Game& game) override
    {
        auto* device = game.getRenderServer().Device();
        if (!device) return;
        auto state = RenderState::CreateOpaque(*device);
        if (!state.Raster() || !state.Depth() || !state.Blend()) return;

        auto geometry = MeshPresets::CreateRectangle(1, 1, 1);
        // The existing opaque state treats clockwise triangles as front faces.
        for (std::size_t triangle = 0; triangle < geometry.indices.size(); triangle += 3)
            std::swap(geometry.indices[triangle + 1], geometry.indices[triangle + 2]);
        for (auto& vertex : geometry.vertices) {
            // Fixed face tones make the existing color-only shader show the box volume.
            const float brightness = 0.28f + 0.72f * (std::max)(0.0f,
                -0.35f * vertex.normal.x + 0.8f * vertex.normal.y - 0.45f * vertex.normal.z);
            vertex.color = {tint.x * brightness, tint.y * brightness, tint.z * brightness, 1};
        }
        renderItem = RenderItemPresets::CreateSimple(device, std::move(geometry), false);
        if (renderItem) *renderItem->material->State() = std::move(state);
    }
};
