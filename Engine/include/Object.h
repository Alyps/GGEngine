#pragma once
#include "TransformTree.h"
#include <vector>
#include "Components.h"

class Game;

class GameObject {
private:
	TransformNode transformTree;

	std::vector<Component*> components;

public:
	// Non-owning: the component must outlive this object's use of it.
	void addComponent(Component& component) { components.push_back(&component); }
	void Init(Game& game);
	TransformNode& fetTransform() { return transformTree;  }
};
