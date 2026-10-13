#include "core.h"
#include "Components.h"
#include "StandartResources/RenderPresets.h"

void RenderComponent::Init(Game& game)
{
	initRenderItem(game);
	game.getRenderServer().addItem(*renderItem);
}

void MeshComponent::initRenderItem(Game& game)
{
	renderItem = RenderItemPresets::CreateSimple(game.getRenderServer().Device(), MeshPresets::CreateRectangle(1.0f, 1.0f, 1.0f), false);
}

