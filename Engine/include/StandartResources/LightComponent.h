#pragma once
#include "TransformableComponent.h"
#include "RenderServer.h"

class LightComponent : public TransformableComponent {
private:
    size_t lightIndex = (std::numeric_limits<size_t>::max)();
protected:
    // Производные компоненты задают тип и параметры независимой записи.
    virtual LightItem makeLightItem() const = 0;
    LightItem commonLightItem() const;
public:
    DirectX::XMFLOAT3 color{ 1, 1, 1 };
    float intensity = 1.0f;
    bool enabled = true;

    void Init(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};

class DirectionalLightComponent : public LightComponent {
protected:
    LightItem makeLightItem() const override;
};

class PointLightComponent : public LightComponent {
protected:
    LightItem makeLightItem() const override;
public:
    float range = 5.0f;
};
