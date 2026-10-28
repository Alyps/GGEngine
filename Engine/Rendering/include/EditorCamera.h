#pragma once
#include "TransformableComponent.h"

class EditorCamera : public TransformableComponent {
private:
    // Вертикальный угол в радианах, расстояния в единицах мира.
    float verticalFov = DirectX::XM_PI / 3.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
public:
    void Init(Game& game) override {}
    void Shutdown(Game& game) override;
    bool getView(DXMatrix& view) const;
    DXMatrix getProjection(float aspectRatio) const;
};
