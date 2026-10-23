#pragma once
#include "TransformableComponent.h"
#include "RenderServer.h"

class RenderComponent : public TransformableComponent {
protected:
    RenderItem* renderItem = nullptr;
    // Материал из ResourceManager заимствуется, процедурный принадлежит компоненту.
    bool ownsMaterial = false;
public:
    virtual void initRenderItem(Game& game) = 0;
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    ~RenderComponent() override;
};
