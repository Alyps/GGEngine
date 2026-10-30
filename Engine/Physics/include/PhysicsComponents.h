#pragma once
#include "TransformableComponent.h"
#include "PhysicsItem.h"

class BoxColliderComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "BoxColliderComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    ColliderItem colliderItem;
public:
    explicit BoxColliderComponent(DXVector3 size = DXVector3::One, PhysicsMaterial material = {});
    ColliderItem& getColliderItem() { return colliderItem; }
    const ColliderItem& getColliderItem() const { return colliderItem; }
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};

class RigidBodyComponent : public TransformableComponent {
    PhysicsItem physicsItem;
protected:
    RigidBodyComponent(PhysicsBodyType type, float mass);
public:
    PhysicsItem& getPhysicsItem() { return physicsItem; }
    const PhysicsItem& getPhysicsItem() const { return physicsItem; }
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};

// Коллайдер добавляется отдельным компонентом того же объекта/корневого узла.
class DynamicRigidBodyComponent : public RigidBodyComponent {
public:
    const char* GetSerializationType() const override { return "DynamicRigidBodyComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    explicit DynamicRigidBodyComponent(float mass = 1.0f)
        : RigidBodyComponent(PhysicsBodyType::Dynamic, mass) {}
};

class StaticRigidBodyComponent : public RigidBodyComponent {
public:
    const char* GetSerializationType() const override { return "StaticRigidBodyComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    StaticRigidBodyComponent() : RigidBodyComponent(PhysicsBodyType::Static, 0.0f) {}
};
