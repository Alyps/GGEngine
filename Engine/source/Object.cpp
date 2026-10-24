#include "Object.h"
#include "Components.h"
#include "core.h"

void GameObject::Init(Game& game)
{
	for (Component* comp : components)
	{
		comp->Init(game);
	}
}