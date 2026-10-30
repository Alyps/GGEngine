#pragma once
#include "RenderComponent.h"
#include "ResourceManager.h"

class MeshComponent : public RenderComponent {
public:
    const char* GetSerializationType() const override { return "MeshComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    enum class GeometryPreset { None, Cube, Sphere };
private:
    GeometryPreset geometryPreset = GeometryPreset::None;
    MeshData<TexturedVertexType> meshData;
    // Ресурс менеджера той же игры живёт дольше компонента.
    const MeshResource* meshResource = nullptr;
    bool usePresetMaterial = false;
    bool fileMesh = false;
    std::string meshPath, materialPath;
    std::string meshError, materialError;
    // Указатель действителен только между Init и Shutdown; Game владеет компонентом.
    Game* activeGame = nullptr;
    void rebuildRenderItem();
public:
    // Компонент хранит геометрию по значению; без аргумента создаётся куб.
    MeshComponent();
    // Куб со стандартным материалом из JSON вместо CreateSimple.
    explicit MeshComponent(bool usePresetMaterial);
    explicit MeshComponent(MeshData<TexturedVertexType> data);
    explicit MeshComponent(const MeshResource& resource);
    MeshComponent(std::string meshPath, std::string materialPath);
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void initRenderItem(Game& game) override;
    void setMeshPath(std::string path);
    void setMaterialPath(std::string path);
    void setGeometryPreset(GeometryPreset preset);
    GeometryPreset getGeometryPreset() const { return geometryPreset; }
    const std::string& getMeshPath() const { return meshPath; }
    const std::string& getMaterialPath() const { return materialPath; }
    const std::string& getMeshError() const { return meshError; }
    const std::string& getMaterialError() const { return materialError; }
    bool hasProceduralMesh() const { return !fileMesh && !meshResource; }
    // Заимствованные данные для выбора геометрии редактором без чтения GPU-буферов.
    const MeshData<TexturedVertexType>& getMeshData() const {
        return meshResource ? meshResource->GetMeshData() : meshData;
    }
    const RenderItem* getRenderItem() const { return renderItem; }
};
