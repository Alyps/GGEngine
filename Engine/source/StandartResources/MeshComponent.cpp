#include "StandartResources/MeshComponent.h"
#include "StandartResources/RenderPresets.h"
#include "core.h"

MeshComponent::MeshComponent()
    : meshData(MeshPresets::CreateRectangle()) {}

MeshComponent::MeshComponent(ResourceManager& resources)
    : meshData(MeshPresets::CreateRectangle()), presetResources(&resources) {}

MeshComponent::MeshComponent(MeshData<NormalVertexType> data)
    : meshData(std::move(data)) {}

void MeshComponent::initRenderItem(Game& game)
{
    // Импорт включается только для компонентов, которым передан менеджер.
    ownsMaterial = presetResources == nullptr;
    renderItem = presetResources
        ? RenderItemPresets::ImportPreset(game.getRenderServer().Device(), meshData, *presetResources)
        : RenderItemPresets::CreateSimple(game.getRenderServer().Device(), meshData, false);
    // Передаём существующий узел объекта, не создавая отдельной трансформации.
    if (renderItem) renderItem->transformNode = transformNode;
}
