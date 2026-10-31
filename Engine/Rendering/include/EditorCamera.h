#pragma once
#include "TransformableComponent.h"

class Level;

class CameraComponent : public TransformableComponent {
public:
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
protected:
    template<class Camera> static Component* ReadCamera(const rapidjson::Value& json, std::string& error);
private:
    // Вертикальный угол в радианах, расстояния в единицах мира.
    float verticalFov = DirectX::XM_PI / 3.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
public:
    void Init(Game& game) override {}
    void Shutdown(Game& game) override;
    bool getView(DXMatrix& view) const;
    DXMatrix getProjection(float aspectRatio) const;
};

class EditorCamera : public CameraComponent {
public:
    const char* GetSerializationType() const override { return "EditorCamera"; }
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
};

class GameCamera : public CameraComponent {
public:
    const char* GetSerializationType() const override { return "GameCamera"; }
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    static GameCamera* Find(Level& level);
};
