#include "StandartResources/MeshComponent.h"
#include "StandartResources/RenderPresets.h"
#include "core.h"

MeshComponent::MeshComponent()
    : geometryPreset(GeometryPreset::Cube), meshData(MeshPresets::CreateRectangle()) {}

MeshComponent::MeshComponent(bool usePresetMaterial)
    : geometryPreset(GeometryPreset::Cube), meshData(MeshPresets::CreateRectangle()), usePresetMaterial(usePresetMaterial) {}

MeshComponent::MeshComponent(MeshData<NormalVertexType> data)
    : meshData(std::move(data)) {}

MeshComponent::MeshComponent(const MeshResource& resource)
    : meshResource(&resource), usePresetMaterial(true) {}

MeshComponent::MeshComponent(std::string meshPath, std::string materialPath)
    : usePresetMaterial(true), fileMesh(true),
      meshPath(std::move(meshPath)), materialPath(std::move(materialPath)) {}

void MeshComponent::Init(Game& game)
{
    activeGame = &game;
    RenderComponent::Init(game);
}

void MeshComponent::Shutdown(Game& game)
{
    RenderComponent::Shutdown(game);
    activeGame = nullptr;
}

void MeshComponent::rebuildRenderItem()
{
    if (!activeGame) return;
    // Убираем старую запись до освобождения её GPU-ресурсов.
    if (renderItem) activeGame->getRenderServer().removeItem(*renderItem);
    releaseRenderItem();
    initRenderItem(*activeGame);
    if (renderItem) activeGame->getRenderServer().addItem(*renderItem);
}

void MeshComponent::setMeshPath(std::string path)
{
    for (char& c : path) if (c == '\\') c = '/';
    meshPath = std::move(path);
    fileMesh = true;
    geometryPreset = GeometryPreset::None;
    // После явного назначения пути процедурная геометрия больше не служит запасной.
    meshResource = nullptr;
    meshData = {};
    rebuildRenderItem();
}

void MeshComponent::setGeometryPreset(GeometryPreset preset)
{
    if (preset == GeometryPreset::None) { setMeshPath(""); return; }
    // Пресет заменяет файловую геометрию; выбранный материал остаётся у компонента.
    meshPath.clear();
    meshResource = nullptr;
    fileMesh = false;
    geometryPreset = preset;
    meshData = preset == GeometryPreset::Cube
        ? MeshPresets::CreateRectangle() : MeshPresets::CreateCircle(0.5f);
    meshError.clear();
    rebuildRenderItem();
}

void MeshComponent::setMaterialPath(std::string path)
{
    for (char& c : path) if (c == '\\') c = '/';
    materialPath = std::move(path);
    usePresetMaterial = true;
    rebuildRenderItem();
}

void MeshComponent::initRenderItem(Game& game)
{
    meshError.clear();
    materialError.clear();
    auto& resources = game.getResourceManager();
    ID3D11Device* device = game.getRenderServer().Device();
    if (fileMesh) {
        // Не сохраняем старую геометрию при ошибке нового пути.
        meshResource = meshPath.empty() ? nullptr : resources.load<MeshResource>(meshPath);
        if (!meshPath.empty() && !meshResource) meshError = resources.GetLastError();
    }
    MaterialResource* materialResource = nullptr;
    if (!materialPath.empty()) {
        materialResource = resources.load<MaterialResource>(materialPath, device);
        if (!materialResource) materialError = resources.GetLastError();
    }
    // Пустой меш ничего не рисует, но путь материала всё равно проверяется.
    const auto& data = getMeshData();
    if (data.vertices.empty() && data.indices.empty()) return;
    if (!data.IsValid()) { meshError = "Invalid mesh geometry; using an empty mesh."; return; }
    if (materialResource) {
        auto mesh = Mesh::Create(device, data);
        if (mesh.IsValid()) {
            auto* gpuMesh = new (std::nothrow) Mesh(std::move(mesh));
            if (gpuMesh) {
                renderItem = new (std::nothrow) RenderItem{ gpuMesh, &materialResource->GetMaterial() };
                if (!renderItem) delete gpuMesh;
            }
        }
        ownsMaterial = false;
    } else {
        // Ошибочный пользовательский материал заменяем стандартным из JSON.
        if (usePresetMaterial) renderItem = RenderItemPresets::ImportPreset(device, data, resources);
        ownsMaterial = false;
        if (!renderItem) {
            // При повреждении штатного JSON остаётся встроенный материал.
            renderItem = RenderItemPresets::CreateSimple(device, data, false);
            ownsMaterial = renderItem != nullptr;
        }
    }
    if (renderItem) renderItem->transformNode = transformNode;
    else meshError = "Cannot create rendering resources; using an empty mesh.";
}
