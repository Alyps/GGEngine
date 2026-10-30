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

rapidjson::Value BoxColliderComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    const auto& item = getColliderItem();
    json.AddMember("size", Serialization::Array({ item.size.x, item.size.y, item.size.z }, allocator), allocator);
    json.AddMember("enabled", item.enabled, allocator);
    // Сохраняем параметры материала, без регистрации коллайдера в PhysicsServer.
    rapidjson::Value material(rapidjson::kObjectType);
    material.AddMember("static_friction", item.material.staticFriction, allocator);
    material.AddMember("dynamic_friction", item.material.dynamicFriction, allocator);
    material.AddMember("restitution", item.material.restitution, allocator);
    json.AddMember("material", material, allocator);
    return Serialization::Finish(std::move(json), error);
}

rapidjson::Value DynamicRigidBodyComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // Скорости и состояние физического актёра относятся к текущей симуляции.
    json.AddMember("enabled", getPhysicsItem().enabled, allocator);
    json.AddMember("mass", getPhysicsItem().mass, allocator);
    return Serialization::Finish(std::move(json), error);
}

rapidjson::Value StaticRigidBodyComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // Скорости и состояние физического актёра относятся к текущей симуляции.
    json.AddMember("enabled", getPhysicsItem().enabled, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* BoxColliderComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    float size[3];
    bool enabled;
    PhysicsMaterial material;
    if (!Floats(json, "size", size, 3, error) || !Bool(json, "enabled", enabled, error)) return nullptr;
    const auto* settings = Field(json, "material", error);
    if (!settings || !Float(*settings, "static_friction", material.staticFriction, error) ||
        !Float(*settings, "dynamic_friction", material.dynamicFriction, error) ||
        !Float(*settings, "restitution", material.restitution, error)) return nullptr;
    if (size[0] <= 0 || size[1] <= 0 || size[2] <= 0 || material.staticFriction < 0 ||
        material.dynamicFriction < 0 || material.restitution < 0 || material.restitution > 1) {
        error = "Invalid collider size or physics material"; return nullptr;
    }
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) BoxColliderComponent(DXVector3(size[0], size[1], size[2]), material);
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->getColliderItem().enabled = enabled;
    return result;
}

Component* DynamicRigidBodyComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    bool enabled;
    if (!Bool(json, "enabled", enabled, error)) return nullptr;
    float mass;
    if (!Float(json, "mass", mass, error)) return nullptr;
    if (mass <= 0) { error = "Mass must be positive"; return nullptr; }
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) DynamicRigidBodyComponent(mass);
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->getPhysicsItem().enabled = enabled;
    return result;
}

Component* StaticRigidBodyComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    bool enabled;
    if (!Bool(json, "enabled", enabled, error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) StaticRigidBodyComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->getPhysicsItem().enabled = enabled;
    return result;
}
