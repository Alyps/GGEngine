#pragma once
#include "TransformTree.h"
#include "RenderServer.h"
//#include "Core.h"
class Game;

class Component {
public:
	virtual ~Component() = default;
	virtual void Init(Game& game) = 0;

};

class TransformableComponent : public Component, public TransformBinding {
};


//Баовый класс для отрисовываемых компонентов. 
// mesh в этом случае - не настриаваемый пользователем. Он настраивается в зависимости от типа компонента.
class RenderComponent : public TransformableComponent {
    friend class Game;
    Game* registeredGame = nullptr;
    RenderComponent* nextRender = nullptr;
    RenderComponent* previousRender = nullptr;
protected:
	RenderItem* renderItem = nullptr;
public:
    // initRenderItem supplies an exclusively owned item, mesh, material, shader and state.
    ~RenderComponent() override;
    void Detach();
    virtual void SyncTransform();
	virtual void initRenderItem(Game& game) = 0;
	virtual void Init(Game& game) override;


};

class MeshComponent : public RenderComponent {
public:
	virtual void initRenderItem(Game& game) override;
};
