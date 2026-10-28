#include "PhysicsComponents.h"
#include "core.h"

BoxColliderComponent::BoxColliderComponent(DXVector3 size, PhysicsMaterial material)
{
    colliderItem.size = size;
    colliderItem.material = material;
}

void BoxColliderComponent::Init(Game& game)
{
    // Первая версия использует корень владельца: он живёт дольше его компонентов.
    TransformNode* root = getOwner() ? &getOwner()->getTransform() : nullptr;
    if (!transformNode) bind_to_node(root);
    if (!root || transformNode != root) {
        game.getPhysicsServer().reportError("box collider must use its owner's root transform");
        return;
    }
    colliderItem.transformNode = transformNode;
    game.getPhysicsServer().addCollider(colliderItem);
}

void BoxColliderComponent::Shutdown(Game& game)
{
    // Система освобождает связанные формы до уничтожения записи компонента.
    game.getPhysicsServer().removeCollider(colliderItem);
    colliderItem.transformNode = nullptr;
}

void BoxColliderComponent::Update(Game& game, double deltaTime)
{
    if (colliderItem.transformNode == transformNode) return;
    Shutdown(game);
    Init(game);
}

RigidBodyComponent::RigidBodyComponent(PhysicsBodyType type, float mass)
{
    physicsItem.type = type;
    physicsItem.mass = mass;
}

void RigidBodyComponent::Init(Game& game)
{
    TransformNode* root = getOwner() ? &getOwner()->getTransform() : nullptr;
    if (!transformNode) bind_to_node(root);
    if (!root || transformNode != root) {
        game.getPhysicsServer().reportError("rigid body must use its owner's root transform");
        return;
    }
    // Item остаётся членом компонента, система хранит только его адрес.
    physicsItem.transformNode = transformNode;
    game.getPhysicsServer().addItem(physicsItem);
}

void RigidBodyComponent::Shutdown(Game& game)
{
    game.getPhysicsServer().removeItem(physicsItem);
    physicsItem.transformNode = nullptr;
}

void RigidBodyComponent::Update(Game& game, double deltaTime)
{
    if (physicsItem.transformNode == transformNode) return;
    Shutdown(game);
    Init(game);
}
