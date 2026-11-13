#pragma once
#include "TransformableComponent.h"
#include "NavigationItem.h"
struct RenderItem;

// Объём, внутри которого генерируется статический навмеш. Box размером size
// в пространстве узла (позиция, поворот и масштаб узла учитываются). Хранит параметры
// агента/Recast и путь к запечённым данным, которые NavigationServer загружает при Init.
class NavMeshVolumeComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "NavMeshVolumeComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    NavMeshVolumeItem volumeItem;
#if defined(GG_EDITOR)
    // Компонент владеет оболочкой и всеми её графическими ресурсами.
    RenderItem* editorShell = nullptr;
    void releaseEditorShell();
#endif
public:
    NavMeshVolumeComponent() = default;
    explicit NavMeshVolumeComponent(const DXVector3& size);
    ~NavMeshVolumeComponent() override;
    // Изменения применяются к Item без перерегистрации; навмеш перестраивается только генератором.
    bool setSize(const DXVector3& size);
    bool setSettings(const NavBuildSettings& settings, std::string* error = nullptr);
    void setDataPath(const std::string& path) { volumeItem.dataPath = path; }
    const DXVector3& getSize() const { return volumeItem.size; }
    const NavBuildSettings& getSettings() const { return volumeItem.settings; }
    const std::string& getDataPath() const { return volumeItem.dataPath; }
    const NavMeshVolumeItem& getVolumeItem() const { return volumeItem; }
    // Применяет размер и параметры проверенного описания к существующему компоненту (Undo редактора),
    // сохраняя владельца, Init и путь данных.
    bool ApplyDescription(const rapidjson::Value& json, std::string& error);
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
