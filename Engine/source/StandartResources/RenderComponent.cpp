#include "StandartResources/RenderComponent.h"
#include "core.h"

void RenderComponent::Init(Game& game)
{
    initRenderItem(game);
    // Ошибка создания геометрии не должна приводить к разыменованию nullptr.
    if (renderItem) game.getRenderServer().addItem(*renderItem);
}
