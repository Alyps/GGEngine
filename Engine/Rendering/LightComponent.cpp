#include "LightComponent.h"
#include "core.h"

LightItem LightComponent::commonLightItem() const
{
    LightItem item;
    item.color = color;
    item.intensity = (std::max)(0.0f, intensity);
    item.enabled = enabled && transformNode;
    return item;
}

void LightComponent::Init(Game& game)
{
    lightItem = makeLightItem();
    game.getRenderServer().addLight(lightItem);
}

void LightComponent::Shutdown(Game& game)
{
    game.getRenderServer().removeLight(lightItem);
}

void LightComponent::Update(Game& game, double deltaTime)
{
    // Адрес записи остаётся прежним; рендер читает актуальные данные.
    lightItem = makeLightItem();
}
LightItem DirectionalLightComponent::makeLightItem() const
{
    LightItem item = commonLightItem();
    item.type = LightType::Directional;
    if (!item.enabled) return item;

    // Из мировой матрицы берём только вращение: масштаб не меняет свет.
    DXMatrix world = transformNode->worldMatrix();
    DXVector3 scale, position;
    DXQuaternion rotation;
    if (!world.Decompose(scale, rotation, position)) {
        item.enabled = false;
        return item;
    }
    rotation.Normalize();
    item.direction = DXVector3::Transform(DXVector3(0, 0, -1), rotation);
    return item;
}

LightItem PointLightComponent::makeLightItem() const
{
    LightItem item = commonLightItem();
    item.type = LightType::Point;
    item.range = (std::max)(0.0f, range);
    item.enabled = item.enabled && item.range > 0.0f;

    // Радиус задаётся явно и не зависит от масштаба узла.
    if (item.enabled) {
        const DXMatrix world = transformNode->worldMatrix();
        item.position = world.Translation();
    }
    return item;
}

rapidjson::Value DirectionalLightComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    json.AddMember("color", Serialization::Array({ color.x, color.y, color.z }, allocator), allocator);
    json.AddMember("intensity", intensity, allocator);
    json.AddMember("enabled", enabled, allocator);
    return Serialization::Finish(std::move(json), error);
}

rapidjson::Value PointLightComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    json.AddMember("color", Serialization::Array({ color.x, color.y, color.z }, allocator), allocator);
    json.AddMember("intensity", intensity, allocator);
    json.AddMember("enabled", enabled, allocator);
    json.AddMember("range", range, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* DirectionalLightComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    float colorValue[3], intensityValue;
    bool enabledValue;
    if (!Floats(json, "color", colorValue, 3, error) || !Float(json, "intensity", intensityValue, error) ||
        !Bool(json, "enabled", enabledValue, error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) DirectionalLightComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->color = { colorValue[0], colorValue[1], colorValue[2] };
    result->intensity = intensityValue;
    result->enabled = enabledValue;
    return result;
}

Component* PointLightComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    float colorValue[3], intensityValue;
    bool enabledValue;
    if (!Floats(json, "color", colorValue, 3, error) || !Float(json, "intensity", intensityValue, error) ||
        !Bool(json, "enabled", enabledValue, error)) return nullptr;
    float rangeValue;
    if (!Float(json, "range", rangeValue, error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) PointLightComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->color = { colorValue[0], colorValue[1], colorValue[2] };
    result->intensity = intensityValue;
    result->enabled = enabledValue;
    result->range = rangeValue;
    return result;
}
