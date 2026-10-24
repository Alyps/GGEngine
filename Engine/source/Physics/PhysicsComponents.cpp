#include "Physics/PhysicsComponents.h"
#include "Physics/PhysicsWorld.h"
#include "core.h"
#include <PxConfig.h>
#include <PxPhysicsAPI.h>

RigidBodyComponent::RigidBodyComponent(const BoxColliderComponent& collider, bool dynamic, float mass)
    : colliderSize(collider.size), material(collider.material), dynamic(dynamic), mass(mass) {}

RigidBodyComponent::~RigidBodyComponent() { Detach(); }
void RigidBodyComponent::OnTransformDetached() { Detach(); }
void RigidBodyComponent::Detach() { if (world) world->RemoveBody(*this); }

bool RigidBodyComponent::Attach(PhysicsWorld& physics)
{
    if (world) return world == &physics;
    return physics.AddBody(*this);
}

void RigidBodyComponent::Init(Game& game)
{
    if (!Attach(game.getPhysicsWorld())) game.reportInitializationFailure();
}

bool RigidBodyComponent::IsSleeping() const
{
    return actor && dynamic && static_cast<physx::PxRigidDynamic*>(actor)->isSleeping();
}

float RigidBodyComponent::GetMass() const
{
    return actor && dynamic ? static_cast<physx::PxRigidDynamic*>(actor)->getMass() : 0.0f;
}

DXVector3 RigidBodyComponent::GetInertia() const
{
    if (!actor || !dynamic) return DXVector3::Zero;
    const auto value = static_cast<physx::PxRigidDynamic*>(actor)->getMassSpaceInertiaTensor();
    return {value.x, value.y, value.z};
}
