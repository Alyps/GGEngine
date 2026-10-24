#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsComponents.h"
#include <PxConfig.h>
#include <PxPhysicsAPI.h>
#include <cmath>
#include <cstdio>
#include <new>

using namespace physx;

namespace {
    bool Error(const char* message) {
        std::fprintf(stderr, "Physics: %s\n", message);
        return false;
    }
    bool Finite(const DXVector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    bool Positive(const DXVector3& v) {
        return Finite(v) && v.x > 0 && v.y > 0 && v.z > 0;
    }
    bool ValidRotation(const DXQuaternion& q) {
        const float length = q.LengthSquared();
        return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
            std::isfinite(q.w) && std::isfinite(length) && std::fabs(length - 1.0f) < 0.001f;
    }
    PxTransform Pose(const TransformNode& node) {
        const auto& p = node.get_position();
        const auto& q = node.get_rotation();
        return PxTransform(PxVec3(p.x, p.y, p.z), PxQuat(q.x, q.y, q.z, q.w).getNormalized());
    }
}

struct PhysicsWorld::Impl {
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    RigidBodyComponent* bodies = nullptr;
    ~Impl() {
        if (scene) scene->release();
        if (dispatcher) dispatcher->release();
        if (physics) physics->release();
        if (foundation) foundation->release();
    }
};

PhysicsWorld::~PhysicsWorld() { Shutdown(); }

bool PhysicsWorld::Init(const DXVector3& gravity)
{
    if (impl) return true;
    if (!Finite(gravity)) return Error("gravity must be finite");
    Impl* created = new (std::nothrow) Impl;
    if (!created) return Error("cannot allocate SDK state");
    created->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, created->allocator, created->errors);
    if (created->foundation)
        created->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *created->foundation, PxTolerancesScale(), false, nullptr);
    if (created->physics) created->dispatcher = PxDefaultCpuDispatcherCreate(1);
    if (created->dispatcher) {
        PxSceneDesc description(created->physics->getTolerancesScale());
        description.gravity = PxVec3(gravity.x, gravity.y, gravity.z);
        description.cpuDispatcher = created->dispatcher;
        description.filterShader = PxDefaultSimulationFilterShader;
        created->scene = created->physics->createScene(description);
    }
    if (!created->scene) {
        delete created;
        return Error("SDK initialization failed; partial state released");
    }
    impl = created;
    return true;
}

void PhysicsWorld::Shutdown()
{
    if (!impl) return;
    while (impl->bodies) RemoveBody(*impl->bodies);
    delete impl;
    impl = nullptr;
}

bool PhysicsWorld::AddBody(RigidBodyComponent& body)
{
    if (body.world) return body.world == this;
    TransformNode* node = body.transformNode;
    if (!node) return Error("rigid body must be bound to a live TransformNode");
    if (node->get_parent()) return Error("only root physical objects are supported; parented body rejected");
    if (!Finite(node->get_position()) || !ValidRotation(node->get_rotation()))
        return Error("position must be finite and rotation must be a unit quaternion");
    if (!Positive(node->get_scale()) || !Positive(body.colliderSize))
        return Error("box dimensions and transform scale must be finite and positive");
    const auto& m = body.material;
    if (!std::isfinite(m.staticFriction) || m.staticFriction < 0 ||
        !std::isfinite(m.dynamicFriction) || m.dynamicFriction < 0 ||
        !std::isfinite(m.restitution) || m.restitution < 0 || m.restitution > 1)
        return Error("friction must be nonnegative; restitution must be in [0, 1]");
    if (body.dynamic && (!std::isfinite(body.mass) || body.mass <= 0))
        return Error("dynamic body mass must be finite and positive");
    const DXVector3 half = body.colliderSize * node->get_scale() * 0.5f;
    if (!Positive(half)) return Error("scaled box dimensions are invalid");
    if (!impl && !Init()) return false;

    PxMaterial* material = impl->physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
    if (!material) return Error("material creation failed");
    PxShape* shape = impl->physics->createShape(PxBoxGeometry(half.x, half.y, half.z), *material, true);
    material->release(); // The shape retains the material.
    if (!shape) return Error("box shape creation failed");
    shape->setContactOffset((std::min)(0.002f, (std::min)({half.x, half.y, half.z}) * 0.1f));
    shape->setRestOffset(0.0f);
    PxRigidActor* actor = body.dynamic
        ? static_cast<PxRigidActor*>(impl->physics->createRigidDynamic(Pose(*node)))
        : static_cast<PxRigidActor*>(impl->physics->createRigidStatic(Pose(*node)));
    const bool attached = actor && actor->attachShape(*shape);
    shape->release(); // The actor retains the shape on successful attachment.
    if (!attached) {
        if (actor) actor->release();
        return Error("actor/shape attachment failed");
    }
    if (body.dynamic) {
        auto* dynamic = static_cast<PxRigidDynamic*>(actor);
        if (!PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, body.mass)) {
            actor->release();
            return Error("mass/inertia calculation failed");
        }
        dynamic->setSolverIterationCounts(8, 2);
    }
    impl->scene->addActor(*actor);
    body.actor = actor;
    body.world = this;
    body.initialScale = node->get_scale();
    body.initialPosition = node->get_position();
    body.initialRotation = node->get_rotation();
    body.previousBody = nullptr;
    body.nextBody = impl->bodies;
    if (impl->bodies) impl->bodies->previousBody = &body;
    impl->bodies = &body;
    return true;
}

void PhysicsWorld::RemoveBody(RigidBodyComponent& body)
{
    if (body.world != this) return;
    if (body.previousBody) body.previousBody->nextBody = body.nextBody;
    else impl->bodies = body.nextBody;
    if (body.nextBody) body.nextBody->previousBody = body.previousBody;
    body.actor->release(); // Also releases the exclusive shape and its material reference.
    body.actor = nullptr;
    body.world = nullptr;
    body.nextBody = body.previousBody = nullptr;
}

bool PhysicsWorld::Step(double seconds)
{
    if (!impl) return true; // No physical objects: ordinary scenes keep their previous behavior.
    const float dt = static_cast<float>(seconds);
    if (!std::isfinite(seconds) || !std::isfinite(dt) || dt <= 0)
        return Error("step interval must be finite and positive");
    for (RigidBodyComponent* body = impl->bodies; body; body = body->nextBody) {
        TransformNode& node = *body->transformNode;
        if (node.get_parent() || node.get_scale() != body->initialScale)
            return Error("parent/scale changes require detaching and recreating the physical body");
        if (!body->dynamic && (node.get_position() != body->initialPosition || node.get_rotation() != body->initialRotation))
            return Error("static pose changes require detaching and recreating the physical body");
    }
    impl->scene->simulate(dt);
    PxU32 error = 0;
    if (!impl->scene->fetchResults(true, &error) || error)
        return Error("fetchResults failed");
    for (RigidBodyComponent* body = impl->bodies; body; body = body->nextBody) {
        if (!body->dynamic) continue;
        const PxTransform pose = body->actor->getGlobalPose();
        body->transformNode->set_position({pose.p.x, pose.p.y, pose.p.z});
        body->transformNode->set_rotation({pose.q.x, pose.q.y, pose.q.z, pose.q.w});
    }
    return true;
}
