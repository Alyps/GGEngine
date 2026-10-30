#include "MeshComponent.h"
#include "RenderPresets.h"
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
        materialResource = resources.load<MaterialResource>(
            materialPath, device, resources.GetStandardResourceDirectory());
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
            renderItem = RenderItemPresets::CreateSimple(
                device, data, false, resources.GetStandardResourceDirectory());
            ownsMaterial = renderItem != nullptr;
        }
    }
    if (renderItem) renderItem->transformNode = transformNode;
    else meshError = "Cannot create rendering resources; using an empty mesh.";
}

rapidjson::Value MeshComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    rapidjson::Value geometry(rapidjson::kObjectType);
    if (fileMesh) {
        geometry.AddMember("source", "file", allocator);
        geometry.AddMember("path", Serialization::String(meshPath, allocator), allocator);
    } else if (geometryPreset != GeometryPreset::None) {
        geometry.AddMember("source", "preset", allocator);
        geometry.AddMember("preset", Serialization::String(geometryPreset == GeometryPreset::Cube ? "Cube" : "Sphere", allocator), allocator);
    } else {
        // Произвольный MeshData и ресурс без доступного пути сохраняем полностью.
        geometry.AddMember("source", "inline", allocator);
        rapidjson::Value vertices(rapidjson::kArrayType), indices(rapidjson::kArrayType);
        const auto& data = getMeshData();
        // Топология нужна, чтобы линии и точки не восстанавливались как треугольники.
        const char* topology = nullptr;
        switch (data.topology) {
        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: topology = "point_list"; break;
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: topology = "line_list"; break;
        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP: topology = "line_strip"; break;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST: topology = "triangle_list"; break;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: topology = "triangle_strip"; break;
        default: error = "Unsupported mesh topology"; return {};
        }
        geometry.AddMember("topology", Serialization::String(topology, allocator), allocator);
        for (const auto& vertex : data.vertices) {
            rapidjson::Value entry(rapidjson::kObjectType);
            const auto& p = vertex.position;
            const auto& n = vertex.normal;
            const auto& c = vertex.color;
            entry.AddMember("position", Serialization::Array({ p.x, p.y, p.z }, allocator), allocator);
            entry.AddMember("normal", Serialization::Array({ n.x, n.y, n.z }, allocator), allocator);
            entry.AddMember("color", Serialization::Array({ c.x, c.y, c.z, c.w }, allocator), allocator);
            vertices.PushBack(entry, allocator);
        }
        for (auto index : data.indices) indices.PushBack(index, allocator);
        geometry.AddMember("vertices", vertices, allocator);
        geometry.AddMember("indices", indices, allocator);
    }
    json.AddMember("geometry", geometry, allocator);
    // Пустой путь материала сохраняет выбор между стандартным и встроенным материалом.
    json.AddMember("material_path", Serialization::String(materialPath, allocator), allocator);
    json.AddMember("use_preset_material", usePresetMaterial, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* MeshComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    std::string source, materialPath;
    bool presetMaterial;
    const auto* geometry = Field(json, "geometry", error);
    if (!geometry || !String(*geometry, "source", source, error) ||
        !String(json, "material_path", materialPath, error) ||
        !Bool(json, "use_preset_material", presetMaterial, error)) return nullptr;
    MeshComponent* result = nullptr;
    if (source == "file") {
        std::string path;
        if (!String(*geometry, "path", path, error)) return nullptr;
        result = new (std::nothrow) MeshComponent(std::move(path), materialPath);
    } else if (source == "preset") {
        std::string preset;
        if (!String(*geometry, "preset", preset, error)) return nullptr;
        if (preset != "Cube" && preset != "Sphere") { error = "Unknown geometry preset: " + preset; return nullptr; }
        result = new (std::nothrow) MeshComponent();
        if (result) result->setGeometryPreset(preset == "Cube" ? GeometryPreset::Cube : GeometryPreset::Sphere);
    } else if (source == "inline") {
        const auto* vertices = Array(*geometry, "vertices", error);
        if (!vertices) return nullptr;
        const auto* indices = Array(*geometry, "indices", error);
        if (!indices) return nullptr;
        // Проверяем ограничения GPU-буферов до выделения памяти под геометрию.
        if (vertices->Size() > (std::numeric_limits<UINT>::max)() / sizeof(NormalVertexType::Vertex) ||
            indices->Size() > (std::numeric_limits<UINT>::max)() / sizeof(uint32_t)) {
            error = "Invalid inline mesh buffer size"; return nullptr;
        }
        MeshData<NormalVertexType> data;
        // Ранние файлы не содержали топологию и использовали список треугольников.
        std::string topology = "triangle_list";
        if (geometry->HasMember("topology") && !String(*geometry, "topology", topology, error)) return nullptr;
        if (topology == "point_list") data.topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        else if (topology == "line_list") data.topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        else if (topology == "line_strip") data.topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        else if (topology == "triangle_list") data.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        else if (topology == "triangle_strip") data.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        else { error = "Unsupported mesh topology: " + topology; return nullptr; }
        data.vertices.reserve(vertices->Size());
        data.indices.reserve(indices->Size());
        for (rapidjson::SizeType i = 0; i < vertices->Size(); ++i) {
            float p[3], n[3], c[4];
            const auto& entry = (*vertices)[i];
            if (!Floats(entry, "position", p, 3, error) || !Floats(entry, "normal", n, 3, error) ||
                !Floats(entry, "color", c, 4, error)) {
                error = "vertices[" + std::to_string(i) + "]: " + error; return nullptr;
            }
            data.vertices.push_back(NormalVertexType::make({ p[0], p[1], p[2] },
                { n[0], n[1], n[2] }, { c[0], c[1], c[2], c[3] }));
        }
        for (const auto& entry : indices->GetArray()) {
            if (!entry.IsUint() || entry.GetUint() >= vertices->Size()) {
                error = "Inline mesh index is out of range"; return nullptr;
            }
            data.indices.push_back(entry.GetUint());
        }
        // Полностью пустая геометрия допустима; частично заполненная должна быть валидной.
        if ((!data.vertices.empty() || !data.indices.empty()) && !data.IsValid()) {
            error = "Invalid inline mesh geometry"; return nullptr;
        }
        result = new (std::nothrow) MeshComponent(std::move(data));
    } else { error = "Unknown geometry source: " + source; return nullptr; }
    if (!result) { error = "Cannot allocate MeshComponent"; return nullptr; }
    // До Init сохраняем только описание: ресурсы и RenderItem ещё не создаются.
    result->materialPath = std::move(materialPath);
    result->usePresetMaterial = presetMaterial;
    return result;
}
