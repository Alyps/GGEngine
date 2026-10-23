#include "StandartResources/RenderComponent.h"
#include "core.h"

void RenderComponent::Init(Game& game)
{
    initRenderItem(game);
    // Ошибка создания геометрии не должна приводить к разыменованию nullptr.
    if (renderItem) game.getRenderServer().addItem(*renderItem);
}

void RenderComponent::Shutdown(Game& game)
{
    if (renderItem) game.getRenderServer().removeItem(*renderItem);
}

RenderComponent::~RenderComponent()
{
    if (!renderItem) return;
    // Кешированные материалы не освобождаем вместе с компонентом.
    if (ownsMaterial && renderItem->material) {
        delete renderItem->material->Shader();
        delete renderItem->material->State();
        delete renderItem->material;
    }
    delete renderItem->mesh;
    delete renderItem;
}
