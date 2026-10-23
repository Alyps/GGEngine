#pragma once
#include "TransformableComponent.h"
#include "RenderServer.h"

class RenderComponent : public TransformableComponent {
protected:
    RenderItem* renderItem = nullptr;
public:
    virtual void initRenderItem(Game& game) = 0;
    void Init(Game& game) override;
};
