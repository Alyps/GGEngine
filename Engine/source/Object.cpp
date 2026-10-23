#include "Object.h"
#include "Components.h"
#include "core.h"

void GameObject::Update(Game& game, double deltaTime)
{
	// Порядок компонентов определяет порядок их покадровой логики.
	for (Component* component : components)
		component->Update(game, deltaTime);
}

void GameObject::Init(Game& game)
{
	for (Component* comp : components)
	{
		comp->Init(game);
	}
}
