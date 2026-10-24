#include "PhysicsServer.h"
#include "PhysicsItem.h"
#include "StandartResources/PhysicsComponents.h"
#include "core.h"
#include <type_traits>

// Только компиляция по Agents.md: ни симуляция, ни приложение не запускаются.
static_assert(!std::is_copy_constructible_v<PhysicsServer>);
static_assert(!std::is_copy_constructible_v<PhysicsItem>);
static_assert(!std::is_copy_constructible_v<ColliderItem>);
static_assert(std::is_base_of_v<Component, DynamicRigidBodyComponent>);
static_assert(std::is_base_of_v<Component, StaticRigidBodyComponent>);
static_assert(std::is_base_of_v<TransformableComponent, BoxColliderComponent>);

// Проверяем создание физических компонентов через нового владельца-объект.
void CompilePhysicsComponentAPI(Level& level)
{
    GameObject* object = level.createObject();
    if (!object) return;
    auto* body = object->createComponent<DynamicRigidBodyComponent>(2.0f);
    auto* collider = object->createComponent<BoxColliderComponent>(DXVector3(1, 2, 3));
    if (!body || !collider) return;
    body->getPhysicsItem().linearVelocity = {0, -1, 0};
    collider->getColliderItem().material.restitution = 0.2f;
    body->bind_to_node(&object->getTransform());
    collider->bind_to_node(&object->getTransform());
    // Удаление откладывает владелец: компонент сам не удаляет себя.
    object->destroyComponent(collider);
    level.destroyObject(object);
}

// Система принимает записи данных, не классы компонентов и не владение ими.
void CompilePhysicsSystemAPI(PhysicsServer& server, PhysicsItem& body, ColliderItem& collider)
{
    if (server.addCollider(collider)) server.removeCollider(collider);
    if (server.addItem(body)) server.removeItem(body);
}

static_assert(std::is_same_v<decltype(RenderItem::transformNode), const TransformNode*>);
static_assert(std::is_same_v<decltype(std::declval<Game&>().getPhysicsServer()), PhysicsServer&>);
