#pragma once
#include "Components.h"

class PhysicsWorld;
namespace physx { class PxRigidActor; }

struct PhysicsMaterial {
    float staticFriction = 0.6f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;
};

class BoxColliderComponent : public Component {
public:
    // Full local dimensions, independent of mesh data. Positive node scale is applied once.
    explicit BoxColliderComponent(DXVector3 size, PhysicsMaterial material = {})
        : size(size), material(material) {}
    DXVector3 size;
    PhysicsMaterial material;
    void Init(Game&) override {}
};

class RigidBodyComponent : public TransformableComponent {
    friend class PhysicsWorld;
    PhysicsWorld* world = nullptr;
    physx::PxRigidActor* actor = nullptr;
    RigidBodyComponent* nextBody = nullptr;
    RigidBodyComponent* previousBody = nullptr;
    DXVector3 colliderSize;
    PhysicsMaterial material;
    DXVector3 initialScale;
    DXVector3 initialPosition;
    DXQuaternion initialRotation;
    bool dynamic;
    float mass;
protected:
    RigidBodyComponent(const BoxColliderComponent& collider, bool dynamic, float mass);
    void OnTransformDetached() override;
public:
    ~RigidBodyComponent() override;
    void Init(Game& game) override;
    bool Attach(PhysicsWorld& physics);
    void Detach();
    bool IsAttached() const noexcept { return world != nullptr; }
    bool IsSleeping() const;
    float GetMass() const;
    DXVector3 GetInertia() const;
};

class DynamicRigidBodyComponent : public RigidBodyComponent {
public:
    // Collider settings are copied here; the collider may be destroyed independently.
    explicit DynamicRigidBodyComponent(const BoxColliderComponent& collider, float mass = 1.0f)
        : RigidBodyComponent(collider, true, mass) {}
};

class StaticRigidBodyComponent : public RigidBodyComponent {
public:
    explicit StaticRigidBodyComponent(const BoxColliderComponent& collider)
        : RigidBodyComponent(collider, false, 0.0f) {}
};
