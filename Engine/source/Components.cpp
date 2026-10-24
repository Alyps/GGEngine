#include "core.h"
#include "Components.h"
#include "StandartResources/RenderPresets.h"

void RenderComponent::Init(Game& game)
{
	if (registeredGame) return;
	if (!renderItem) initRenderItem(game);
	if (!renderItem) {
		std::cerr << "RenderComponent: render item creation failed\n";
		game.reportInitializationFailure();
		return;
	}
	SyncTransform();
	game.getRenderServer().addItem(*renderItem);
	game.registerRenderComponent(*this);
}

void RenderComponent::SyncTransform()
{
	if (renderItem && transformNode) renderItem->world = transformNode->worldMatrix();
}

void RenderComponent::Detach()
{
	if (!registeredGame) return;
	registeredGame->getRenderServer().removeItem(*renderItem);
	registeredGame->unregisterRenderComponent(*this);
}

RenderComponent::~RenderComponent()
{
	Detach();
	if (!renderItem) return;
	if (renderItem->material) {
		delete renderItem->material->Shader();
		delete renderItem->material->State();
		delete renderItem->material;
	}
	delete renderItem->mesh;
	delete renderItem;
}

void MeshComponent::initRenderItem(Game& game)
{
	renderItem = RenderItemPresets::CreateSimple(game.getRenderServer().Device(), MeshPresets::CreateRectangle(1.0f, 1.0f, 1.0f), false);
}

