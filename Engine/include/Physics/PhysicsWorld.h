#pragma once
#include "GGMath.h"

class RigidBodyComponent;

// Owns the SDK and all registered actors. Components are non-owning bindings.
class PhysicsWorld {
    struct Impl;
    Impl* impl = nullptr;
public:
    PhysicsWorld() = default;
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    bool Init(const DXVector3& gravity = DXVector3(0, -9.81f, 0));
    void Shutdown();
    bool IsInitialized() const noexcept { return impl != nullptr; }
    bool AddBody(RigidBodyComponent& body);
    void RemoveBody(RigidBodyComponent& body);
    bool Step(double seconds);
};
