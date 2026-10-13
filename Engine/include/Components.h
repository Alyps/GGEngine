#pragma once
#include "TransformTree.h"
#include "RenderServer.h"
//#include "Core.h"
class Game;

class Component {
public:
	virtual void Init(Game& game) = 0;

};

class TransformableComponent : public Component {
protected:
	TransformNode* transformNode;

public:
	void bind_to_node(TransformNode* node) {
		transformNode = node;
	}
};


//Баовый класс для отрисовываемых компонентов. 
// mesh в этом случае - не настриаваемый пользователем. Он настраивается в зависимости от типа компонента.
class RenderComponent : public TransformableComponent {
protected:
	RenderItem* renderItem;
public:
	virtual void initRenderItem(Game& game) = 0;
	virtual void Init(Game& game) override;


};

class MeshComponent : public RenderComponent {
public:
	virtual void initRenderItem(Game& game) override;
};