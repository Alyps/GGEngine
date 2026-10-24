#pragma once
#include "RenderComponent.h"

class ResourceManager;

class MeshComponent : public RenderComponent {
private:
    MeshData<NormalVertexType> meshData;
    // Невладеющая ссылка: менеджер живёт дольше компонента и рендера.
    ResourceManager* presetResources = nullptr;
public:
    // Компонент хранит геометрию по значению; без аргумента создаётся куб.
    MeshComponent();
    // Куб со стандартным материалом из JSON вместо CreateSimple.
    explicit MeshComponent(ResourceManager& resources);
    explicit MeshComponent(MeshData<NormalVertexType> data);
    void initRenderItem(Game& game) override;
    // Заимствованные данные для выбора геометрии редактором без чтения GPU-буферов.
    const MeshData<NormalVertexType>& getMeshData() const { return meshData; }
    const RenderItem* getRenderItem() const { return renderItem; }
};
