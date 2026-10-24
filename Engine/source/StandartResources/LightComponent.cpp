#include "StandartResources/LightComponent.h"
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
