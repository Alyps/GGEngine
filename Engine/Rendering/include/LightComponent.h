#pragma once
#include "TransformableComponent.h"
#include "RenderServer.h"

class LightComponent : public TransformableComponent {
private:
    LightItem lightItem;
protected:
    // Производные компоненты задают тип и параметры независимой записи.
    virtual LightItem makeLightItem() const = 0;
    LightItem commonLightItem() const;
public:
    DirectX::XMFLOAT3 color{ 1, 1, 1 };
    float intensity = 1.0f;
    bool enabled = true;

    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};

class DirectionalLightComponent : public LightComponent {
public:
    const char* GetSerializationType() const override { return "DirectionalLightComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
protected:
    LightItem makeLightItem() const override;
};

class PointLightComponent : public LightComponent {
public:
    const char* GetSerializationType() const override { return "PointLightComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
protected:
    LightItem makeLightItem() const override;
public:
    float range = 5.0f;
};
