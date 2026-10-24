#include "PhysicsServer.h"
#include "TransformTree.h"
#include <PxConfig.h>
#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
#include <vector>

using namespace physx;

namespace {
    bool finite(const DXVector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    bool positive(const DXVector3& v) {
        return finite(v) && v.x > 0 && v.y > 0 && v.z > 0;
    }
    bool validPose(const TransformNode* node) {
        if (!node || node->get_parent() || !finite(node->get_position())) return false;
        const auto& q = node->get_rotation();
        return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
            std::isfinite(q.w) && std::fabs(q.LengthSquared() - 1.0f) < 0.001f;
    }
    bool validMaterial(const PhysicsMaterial& m) {
        return std::isfinite(m.staticFriction) && m.staticFriction >= 0 &&
            std::isfinite(m.dynamicFriction) && m.dynamicFriction >= 0 &&
            std::isfinite(m.restitution) && m.restitution >= 0 && m.restitution <= 1;
    }
    bool sameMaterial(const PhysicsMaterial& a, const PhysicsMaterial& b) {
        return a.staticFriction == b.staticFriction && a.dynamicFriction == b.dynamicFriction &&
            a.restitution == b.restitution;
    }
    PxVec3 vector(const DXVector3& v) { return {v.x, v.y, v.z}; }
    DXVector3 vector(const PxVec3& v) { return {v.x, v.y, v.z}; }
    PxTransform pose(const TransformNode& node) {
        const auto& q = node.get_rotation();
        return {vector(node.get_position()), PxQuat(q.x, q.y, q.z, q.w).getNormalized()};
    }
}

struct PhysicsServer::Impl {
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    // Оба списка заимствуют адреса: владельцами записей остаются компоненты.
    std::vector<PhysicsItem*> items;
    std::vector<ColliderItem*> colliders;
    ~Impl() {
        if (scene) scene->release();
        if (dispatcher) dispatcher->release();
        if (physics) physics->release();
        if (foundation) foundation->release();
    }
};

PhysicsItem::~PhysicsItem() { if (server) server->removeItem(*this); }
ColliderItem::~ColliderItem() { if (server) server->removeCollider(*this); }
PhysicsServer::~PhysicsServer() { Shutdown(); }

bool PhysicsServer::reportError(const char* message)
{
    lastError = message;
    std::fprintf(stderr, "PhysicsServer: %s\n", message);
    return false;
}

bool PhysicsServer::Init(const DXVector3& gravity)
{
    if (impl) return lastError.empty();
    if (!lastError.empty()) return false;
    if (!finite(gravity)) return reportError("gravity must be finite");
    auto* created = new (std::nothrow) Impl;
    if (!created) return reportError("cannot allocate PhysX state");

    // Следующий ресурс создаётся только после успешного предыдущего.
    created->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, created->allocator, created->errors);
    if (created->foundation)
        created->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *created->foundation, PxTolerancesScale(), false, nullptr);
    if (created->physics) created->dispatcher = PxDefaultCpuDispatcherCreate(1);
    if (created->dispatcher) {
        PxSceneDesc description(created->physics->getTolerancesScale());
        description.gravity = vector(gravity);
        description.cpuDispatcher = created->dispatcher;
        description.filterShader = PxDefaultSimulationFilterShader;
        created->scene = created->physics->createScene(description);
    }
    // Деструктор Impl также очищает частично созданный SDK.
    if (!created->scene) { delete created; return reportError("PhysX initialization failed"); }
    impl = created;
    return true;
}

void PhysicsServer::releaseActor(PhysicsItem& item)
{
    // Actor освобождает exclusive shapes, а формы — ссылки на материалы.
    if (item.actor) item.actor->release();
    item.actor = nullptr;
    item.sleeping = false;
    item.rebuild = true;
}

void PhysicsServer::invalidateNode(const TransformNode* node)
{
    if (!impl) return;
    for (PhysicsItem* item : impl->items)
        if (item->transformNode == node) releaseActor(*item);
}

bool PhysicsServer::addItem(PhysicsItem& item)
{
    if (item.server) return item.server == this;
    if (!validPose(item.transformNode)) return reportError("body needs a finite root pose and unit quaternion");
    if (!Init()) return false;
    for (const PhysicsItem* registered : impl->items)
        if (registered->transformNode == item.transformNode)
            return reportError("only one rigid body per root transform is supported");
    // Регистрация возможна до коллайдеров: актёр собирается перед fixed-step.
    impl->items.push_back(&item);
    item.server = this;
    item.rebuild = true;
    return true;
}

void PhysicsServer::removeItem(PhysicsItem& item)
{
    if (item.server != this) return;
    releaseActor(item);
    auto& items = impl->items;
    items.erase(std::remove(items.begin(), items.end(), &item), items.end());
    item.server = nullptr;
}

bool PhysicsServer::addCollider(ColliderItem& item)
{
    if (item.server) return item.server == this;
    if (!validPose(item.transformNode)) return reportError("collider needs a finite root pose and unit quaternion");
    if (!Init()) return false;
    impl->colliders.push_back(&item);
    item.server = this;
    // Новый коллайдер должен попасть в актёр независимо от порядка Init компонентов.
    invalidateNode(item.transformNode);
    return true;
}

void PhysicsServer::removeCollider(ColliderItem& item)
{
    if (item.server != this) return;
    // Сначала снимаем формы, затем забываем адрес удаляемой записи.
    invalidateNode(item.transformNode);
    auto& colliders = impl->colliders;
    colliders.erase(std::remove(colliders.begin(), colliders.end(), &item), colliders.end());
    item.server = nullptr;
}

void PhysicsServer::Shutdown()
{
    if (impl) {
        // Пережившие систему Item остаются валидными и больше не обращаются к ней.
        while (!impl->items.empty()) removeItem(*impl->items.back());
        while (!impl->colliders.empty()) removeCollider(*impl->colliders.back());
        delete impl;
        impl = nullptr;
    }
    lastError.clear();
}

bool PhysicsServer::rebuildActor(PhysicsItem& item)
{
    releaseActor(item);
    item.appliedScale = item.transformNode->get_scale();
    item.appliedType = item.type;
    item.appliedMass = item.mass;
    item.rebuild = false;

    const auto matches = [&item](const ColliderItem* collider) {
        return collider->enabled && collider->transformNode == item.transformNode;
    };
    // Тело без активного коллайдера зарегистрировано, но не участвует в симуляции.
    if (std::none_of(impl->colliders.begin(), impl->colliders.end(), matches)) return true;
    PxRigidActor* actor = item.type == PhysicsBodyType::Dynamic
        ? static_cast<PxRigidActor*>(impl->physics->createRigidDynamic(pose(*item.transformNode)))
        : static_cast<PxRigidActor*>(impl->physics->createRigidStatic(pose(*item.transformNode)));
    if (!actor) return reportError("cannot create rigid actor");

    for (const ColliderItem* collider : impl->colliders) {
        if (!matches(collider)) continue;
        const auto& materialData = collider->material;
        PxMaterial* material = impl->physics->createMaterial(materialData.staticFriction,
            materialData.dynamicFriction, materialData.restitution);
        if (!material) { actor->release(); return reportError("cannot create physics material"); }
        const DXVector3 half = collider->size * item.appliedScale * 0.5f;
        PxShape* shape = impl->physics->createShape(PxBoxGeometry(half.x, half.y, half.z), *material, true);
        material->release(); // Shape удерживает материал после создания.
        if (!shape) { actor->release(); return reportError("cannot create box shape"); }
        shape->setContactOffset((std::min)(0.02f, (std::min)({half.x, half.y, half.z}) * 0.1f));
        shape->setRestOffset(0.0f);
        const bool attached = actor->attachShape(*shape);
        shape->release(); // После attach ссылкой на форму владеет actor.
        if (!attached) { actor->release(); return reportError("cannot attach collider shape"); }
    }
    if (item.type == PhysicsBodyType::Dynamic) {
        auto* body = static_cast<PxRigidDynamic*>(actor);
        if (!PxRigidBodyExt::setMassAndUpdateInertia(*body, item.mass)) {
            actor->release();
            return reportError("cannot calculate mass and inertia");
        }
        // Пересоздание формы после изменения масштаба сохраняет скорости тела.
        body->setLinearVelocity(vector(item.linearVelocity));
        body->setAngularVelocity(vector(item.angularVelocity));
        body->setSolverIterationCounts(8, 2);
    }
    impl->scene->addActor(*actor);
    item.actor = actor;
    item.lastPosition = item.transformNode->get_position();
    item.lastRotation = item.transformNode->get_rotation();
    return true;
}

bool PhysicsServer::Update(double fixedInterval)
{
    if (!lastError.empty()) return false;
    if (!impl) return true; // В обычных сценах SDK не создаётся без физических компонентов.
    const float dt = static_cast<float>(fixedInterval);
    if (!std::isfinite(fixedInterval) || !std::isfinite(dt) || dt <= 0)
        return reportError("fixed interval must be finite and positive");

    for (ColliderItem* collider : impl->colliders) {
        if (collider->enabled && (!validPose(collider->transformNode) || !positive(collider->size) ||
            !positive(collider->transformNode->get_scale()) || !validMaterial(collider->material)))
            return reportError("invalid box collider pose, dimensions, scale or material");
        if (collider->enabled && !positive(collider->size * collider->transformNode->get_scale() * 0.5f))
            return reportError("scaled box half dimensions must be finite and positive");
        // Изменения данных компонента требуют обновить форму, а не графический ресурс.
        if (collider->size != collider->appliedSize || collider->enabled != collider->appliedEnabled ||
            !sameMaterial(collider->material, collider->appliedMaterial)) invalidateNode(collider->transformNode);
        collider->appliedSize = collider->size;
        collider->appliedMaterial = collider->material;
        collider->appliedEnabled = collider->enabled;
    }
    for (PhysicsItem* item : impl->items) {
        if (!item->enabled) { releaseActor(*item); continue; }
        if (!validPose(item->transformNode) || !positive(item->transformNode->get_scale()) ||
            !finite(item->linearVelocity) || !finite(item->angularVelocity))
            return reportError("invalid body root pose, scale or velocity");
        if (item->type != PhysicsBodyType::Static && item->type != PhysicsBodyType::Dynamic)
            return reportError("unknown rigid body type");
        if (item->type == PhysicsBodyType::Dynamic && (!std::isfinite(item->mass) || item->mass <= 0))
            return reportError("dynamic body mass must be finite and positive");
        if (item->rebuild || item->type != item->appliedType || item->mass != item->appliedMass ||
            item->transformNode->get_scale() != item->appliedScale)
            if (!rebuildActor(*item)) return false;
        if (!item->actor) continue;

        // Gizmo/Lua могут менять корневой узел: такую позу передаём в PhysX один раз.
        if (item->transformNode->get_position() != item->lastPosition ||
            item->transformNode->get_rotation() != item->lastRotation)
            item->actor->setGlobalPose(pose(*item->transformNode));
        if (item->type == PhysicsBodyType::Dynamic) {
            auto* body = static_cast<PxRigidDynamic*>(item->actor);
            if (vector(body->getLinearVelocity()) != item->linearVelocity)
                body->setLinearVelocity(vector(item->linearVelocity));
            if (vector(body->getAngularVelocity()) != item->angularVelocity)
                body->setAngularVelocity(vector(item->angularVelocity));
        }
    }
    impl->scene->simulate(dt);
    PxU32 error = 0;
    if (!impl->scene->fetchResults(true, &error) || error) return reportError("fetchResults failed");
    for (PhysicsItem* item : impl->items) {
        if (!item->actor) continue;
        if (item->type == PhysicsBodyType::Dynamic) {
            const auto* body = static_cast<PxRigidDynamic*>(item->actor);
            const PxTransform current = body->getGlobalPose();
            item->transformNode->set_position(vector(current.p));
            item->transformNode->set_rotation({current.q.x, current.q.y, current.q.z, current.q.w});
            // Масштаб узла не меняем; скорости и сон — актуальные данные Item.
            item->linearVelocity = vector(body->getLinearVelocity());
            item->angularVelocity = vector(body->getAngularVelocity());
            item->sleeping = body->isSleeping();
        }
        item->lastPosition = item->transformNode->get_position();
        item->lastRotation = item->transformNode->get_rotation();
    }
    return true;
}
