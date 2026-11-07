#pragma once
#include "TransformableComponent.h"
#include "PhysicsItem.h"
struct RenderItem;

class BoxColliderComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "BoxColliderComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    ColliderItem colliderItem;
#if defined(GG_EDITOR)
    // Компонент владеет оболочкой и всеми её графическими ресурсами.
    RenderItem* editorShell = nullptr;
    void releaseEditorShell();
#endif
public:
    explicit BoxColliderComponent(DXVector3 size = DXVector3::One, PhysicsMaterial material = {});
    ~BoxColliderComponent() override;
    bool setSize(const DXVector3& size);
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

    // C++ API компонента используется Lua-привязками без Game и типов Jolt в аргументах.
    // Мировые векторы; сила/момент действуют один fixed-step, импульсы — один раз.
    // Результат означает принятие команды в Item, а не немедленное изменение скорости.
    bool addForce(const DXVector3& force) { return getPhysicsItem().addForce(force); }
    bool addImpulse(const DXVector3& impulse) { return getPhysicsItem().addImpulse(impulse); }
    bool addTorque(const DXVector3& torque) { return getPhysicsItem().addTorque(torque); }
    bool addAngularImpulse(const DXVector3& impulse) { return getPhysicsItem().addAngularImpulse(impulse); }
};

class StaticRigidBodyComponent : public RigidBodyComponent {
public:
    const char* GetSerializationType() const override { return "StaticRigidBodyComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    StaticRigidBodyComponent() : RigidBodyComponent(PhysicsBodyType::Static, 0.0f) {}
};
