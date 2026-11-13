#include "NavMeshVolumeComponent.h"
#include "core.h"
#include <cmath>
#include <iostream>
#if defined(GG_EDITOR)
#include "RenderPresets.h"
#endif

namespace {
    bool ValidSize(const DXVector3& size)
    {
        return std::isfinite(size.x) && std::isfinite(size.y) && std::isfinite(size.z) &&
            size.x > 0.0f && size.y > 0.0f && size.z > 0.0f;
    }

    // Ключи JSON вещественных настроек; tile_size хранится отдельно как целое.
    struct SettingField {
        const char* name;
        float NavBuildSettings::* field;
    };
    constexpr SettingField SettingFields[] = {
        { "agent_radius", &NavBuildSettings::agentRadius },
        { "agent_height", &NavBuildSettings::agentHeight },
        { "agent_max_climb", &NavBuildSettings::agentMaxClimb },
        { "agent_max_slope", &NavBuildSettings::agentMaxSlope },
        { "cell_size", &NavBuildSettings::cellSize },
        { "cell_height", &NavBuildSettings::cellHeight },
        { "region_min_size", &NavBuildSettings::regionMinSize },
        { "region_merge_size", &NavBuildSettings::regionMergeSize },
        { "edge_max_length", &NavBuildSettings::edgeMaxLength },
        { "edge_max_error", &NavBuildSettings::edgeMaxError },
        { "detail_sample_distance", &NavBuildSettings::detailSampleDistance },
        { "detail_sample_max_error", &NavBuildSettings::detailSampleMaxError },
    };
}

NavMeshVolumeComponent::NavMeshVolumeComponent(const DXVector3& size)
{
    if (ValidSize(size)) volumeItem.size = size;
}

NavMeshVolumeComponent::~NavMeshVolumeComponent()
{
#if defined(GG_EDITOR)
    releaseEditorShell();
#endif
}

#if defined(GG_EDITOR)
void NavMeshVolumeComponent::releaseEditorShell()
{
    if (!editorShell) return;
    // Shutdown снимает регистрацию до освобождения принадлежащих компоненту данных.
    delete editorShell->material->Shader();
    delete editorShell->material->State();
    delete editorShell->material;
    delete editorShell->mesh;
    delete editorShell;
    editorShell = nullptr;
}
#endif

bool NavMeshVolumeComponent::setSize(const DXVector3& size)
{
    // Не принимаем вырожденный объём и нечисловые значения из вызывающего кода.
    if (!ValidSize(size)) return false;
    volumeItem.size = size;
    return true;
}

bool NavMeshVolumeComponent::setSettings(const NavBuildSettings& settings, std::string* error)
{
    std::string reason;
    if (!settings.Validate(reason)) {
        if (error) *error = reason;
        return false;
    }
    volumeItem.settings = settings;
    return true;
}

void NavMeshVolumeComponent::Init(Game& game)
{
    // Объём может стоять на любой ветке; без выбора используется корень владельца.
    TransformNode* root = getOwner() ? &getOwner()->getTransform() : nullptr;
    if (!transformNode) bind_to_node(root);
    if (!transformNode) return;
    volumeItem.transformNode = transformNode;
    // Путь данных задан относительно проекта; первый объём уровня загружает навмеш.
    NavigationServer& navigation = game.getNavigationServer();
    const std::string resolved = volumeItem.dataPath.empty()
        ? std::string{} : game.getResourceManager().ResolvePath(volumeItem.dataPath);
    navigation.addVolume(volumeItem, resolved);
    if (!resolved.empty() && !navigation.hasNavMesh() && !navigation.getLastError().empty())
        std::cout << "[Navigation] " << navigation.getLastError() << std::endl;
#if defined(GG_EDITOR)
    // Единичный бокс масштабируется размером объёма при отрисовке, как у коллайдера.
    editorShell = RenderItemPresets::CreateSimple(game.getRenderServer().Device(),
        MeshPresets::CreateRectangle(), false,
        game.getResourceManager().GetStandardResourceDirectory(), true, "PSNavVolumeMain");
    if (editorShell) {
        editorShell->transformNode = transformNode;
        editorShell->editorShellSize = &volumeItem.size;
        editorShell->editorGame = &game;
        game.getRenderServer().addItem(*editorShell);
    }
#endif
}

void NavMeshVolumeComponent::Shutdown(Game& game)
{
#if defined(GG_EDITOR)
    if (editorShell) game.getRenderServer().removeItem(*editorShell);
    releaseEditorShell();
#endif
    game.getNavigationServer().removeVolume(volumeItem);
    volumeItem.transformNode = nullptr;
}

void NavMeshVolumeComponent::Update(Game& game, double deltaTime)
{
    // Перенос компонента на другую ветку требует новой оболочки и регистрации.
    if (volumeItem.transformNode == transformNode) return;
    Shutdown(game);
    Init(game);
}

rapidjson::Value NavMeshVolumeComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    const auto& item = volumeItem;
    json.AddMember("size", Serialization::Array({ item.size.x, item.size.y, item.size.z }, allocator), allocator);
    json.AddMember("data", Serialization::String(item.dataPath, allocator), allocator);
    // Параметры генерации сохраняются вместе с объёмом; сам навмеш лежит в файле data.
    rapidjson::Value settings(rapidjson::kObjectType);
    for (const auto& field : SettingFields)
        settings.AddMember(rapidjson::StringRef(field.name), item.settings.*field.field, allocator);
    settings.AddMember("tile_size", item.settings.tileSize, allocator);
    json.AddMember("settings", settings, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* NavMeshVolumeComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    float size[3];
    std::string data;
    if (!Floats(json, "size", size, 3, error) || !String(json, "data", data, error)) return nullptr;
    const auto* settingsJson = Field(json, "settings", error);
    if (!settingsJson) return nullptr;
    NavBuildSettings settings;
    for (const auto& field : SettingFields)
        if (!Float(*settingsJson, field.name, settings.*field.field, error)) return nullptr;
    float tileSize = 0.0f;
    if (!Float(*settingsJson, "tile_size", tileSize, error)) return nullptr;
    // Проверяем диапазон до приведения: большое значение float нельзя привести к int.
    if (tileSize < 1.0f || tileSize > 100000.0f || tileSize != std::floor(tileSize)) {
        error = "tile_size: expected a positive integer"; return nullptr;
    }
    settings.tileSize = static_cast<int>(tileSize);
    const DXVector3 volumeSize(size[0], size[1], size[2]);
    if (!ValidSize(volumeSize)) { error = "Invalid navmesh volume size"; return nullptr; }
    if (!settings.Validate(error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) NavMeshVolumeComponent(volumeSize);
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->volumeItem.settings = settings;
    result->volumeItem.dataPath = std::move(data);
    return result;
}

bool NavMeshVolumeComponent::ApplyDescription(const rapidjson::Value& json, std::string& error)
{
    auto* description = static_cast<NavMeshVolumeComponent*>(CreateFromJson(json, error));
    if (!description) return false;
    // Временное описание не проходит Init; живой компонент сохраняет регистрацию и оболочку.
    // Путь данных задаёт генератор, поэтому Undo размера или параметров его не откатывает.
    volumeItem.size = description->volumeItem.size;
    volumeItem.settings = description->volumeItem.settings;
    delete description;
    return true;
}
