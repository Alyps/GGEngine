#include "PhysicsComponents.h"
#include "core.h"
#include <cmath>
#if defined(GG_EDITOR)
#include "RenderPresets.h"
#endif
#include <new>

namespace {
    // Ключи JSON настроек персонажа; поля inspector-а и файл используют одну таблицу.
    struct CharacterField {
        const char* name;
        float CharacterSettings::* field;
    };
    constexpr CharacterField CharacterFields[] = {
        { "radius", &CharacterSettings::radius },
        { "gravity_scale", &CharacterSettings::gravityScale },
        { "max_slope", &CharacterSettings::maxSlope },
        { "step_height", &CharacterSettings::stepHeight },
        { "mass", &CharacterSettings::mass },
        { "max_strength", &CharacterSettings::maxStrength },
    };

    bool FiniteVector(const DXVector3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
}

bool BoxColliderComponent::setSize(const DXVector3& size)
{
    // Не принимаем вырожденный бокс и нечисловые значения из вызывающего кода.
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) ||
        size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) return false;
    colliderItem.size = size;
    return true;
}

BoxColliderComponent::~BoxColliderComponent()
{
#if defined(GG_EDITOR)
    releaseEditorShell();
#endif
}

#if defined(GG_EDITOR)
void BoxColliderComponent::releaseEditorShell()
{
    if (!editorShell) return;
    // Shutdown снимает регистрацию до освобождения принадлежащих компоненту данных.
    delete editorShell->material->Shader();
    delete editorShell->material->State();
    delete editorShell->material;
    delete editorShell->mesh;
    delete editorShell;
    editorShell = nullptr;
}
#endif

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
#if defined(GG_EDITOR)
    // Единичный бокс масштабируется при отрисовке без пересоздания GPU-меша.
    editorShell = RenderItemPresets::CreateSimple(game.getRenderServer().Device(),
        MeshPresets::CreateRectangle(), false,
        game.getResourceManager().GetStandardResourceDirectory(), true, "PSColliderMain");
    if (editorShell) {
        editorShell->transformNode = transformNode;
        editorShell->editorShellSize = &colliderItem.size;
        editorShell->editorGame = &game;
        game.getRenderServer().addItem(*editorShell);
    }
#endif
}

void BoxColliderComponent::Shutdown(Game& game)
{
#if defined(GG_EDITOR)
    if (editorShell) game.getRenderServer().removeItem(*editorShell);
    releaseEditorShell();
#endif
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
    json.AddMember("affects_navmesh", item.affectsNavmesh, allocator);
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
    // Поле необязательно: сцены, сохранённые до его появления, должны загружаться.
    bool affectsNavmesh = true;
    if (json.HasMember("affects_navmesh") && !Bool(json, "affects_navmesh", affectsNavmesh, error)) return nullptr;
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
    result->getColliderItem().affectsNavmesh = affectsNavmesh;
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

rapidjson::Value KinematicRigidBodyComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // Скорости задаются во время игры и относятся к текущей симуляции.
    json.AddMember("enabled", getPhysicsItem().enabled, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* KinematicRigidBodyComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    bool enabled;
    if (!Deserialization::Bool(json, "enabled", enabled, error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) KinematicRigidBodyComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->getPhysicsItem().enabled = enabled;
    return result;
}

bool KinematicRigidBodyComponent::setLinearVelocity(const DXVector3& velocity)
{
    if (!FiniteVector(velocity)) return false;
    getPhysicsItem().linearVelocity = velocity;
    return true;
}

bool KinematicRigidBodyComponent::setAngularVelocity(const DXVector3& velocity)
{
    if (!FiniteVector(velocity)) return false;
    getPhysicsItem().angularVelocity = velocity;
    return true;
}

CharacterMovementComponent::~CharacterMovementComponent()
{
#if defined(GG_EDITOR)
    releaseEditorShell();
#endif
}

#if defined(GG_EDITOR)
void CharacterMovementComponent::releaseEditorShell()
{
    if (!editorShell) return;
    // Shutdown снимает регистрацию до освобождения принадлежащих компоненту данных.
    delete editorShell->material->Shader();
    delete editorShell->material->State();
    delete editorShell->material;
    delete editorShell->mesh;
    delete editorShell;
    editorShell = nullptr;
}

void CharacterMovementComponent::refreshShellSize()
{
    // Отрисовка умножает оболочку на масштаб узла, поэтому заранее делим на него.
    const float diameter = characterItem.settings.radius * 2.0f;
    const DXVector3 scale = transformNode ? transformNode->get_scale() : DXVector3::One;
    const auto fit = [&](float axis) { return diameter / (std::fabs(axis) > 0.00001f ? std::fabs(axis) : 1.0f); };
    shellSize = DXVector3(fit(scale.x), fit(scale.y), fit(scale.z));
}
#endif

bool CharacterMovementComponent::setSettings(const CharacterSettings& settings, std::string* error)
{
    std::string reason;
    if (!settings.Validate(reason)) {
        if (error) *error = reason;
        return false;
    }
    characterItem.settings = settings;
    return true;
}

void CharacterMovementComponent::Init(Game& game)
{
    // Как и коллайдер, персонаж использует корень владельца: он живёт дольше компонентов.
    TransformNode* root = getOwner() ? &getOwner()->getTransform() : nullptr;
    if (!transformNode) bind_to_node(root);
    if (!root || transformNode != root) {
        game.getPhysicsServer().reportError("character movement must use its owner's root transform");
        return;
    }
    characterItem.transformNode = transformNode;
    game.getPhysicsServer().addCharacter(characterItem);
#if defined(GG_EDITOR)
    // Сфера единичного диаметра масштабируется при отрисовке без пересоздания GPU-меша.
    refreshShellSize();
    editorShell = RenderItemPresets::CreateSimple(game.getRenderServer().Device(),
        MeshPresets::CreateCircle(0.5f), false,
        game.getResourceManager().GetStandardResourceDirectory(), true, "PSColliderMain");
    if (editorShell) {
        editorShell->transformNode = transformNode;
        editorShell->editorShellSize = &shellSize;
        editorShell->editorGame = &game;
        game.getRenderServer().addItem(*editorShell);
    }
#endif
}

void CharacterMovementComponent::Shutdown(Game& game)
{
#if defined(GG_EDITOR)
    if (editorShell) game.getRenderServer().removeItem(*editorShell);
    releaseEditorShell();
#endif
    // Система освобождает персонажа Jolt до уничтожения записи компонента.
    game.getPhysicsServer().removeCharacter(characterItem);
    characterItem.transformNode = nullptr;
}

void CharacterMovementComponent::Update(Game& game, double deltaTime)
{
#if defined(GG_EDITOR)
    refreshShellSize();
#endif
    if (characterItem.transformNode == transformNode) return;
    Shutdown(game);
    Init(game);
}

rapidjson::Value CharacterMovementComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    for (const auto& field : CharacterFields)
        json.AddMember(rapidjson::StringRef(field.name), characterItem.settings.*field.field, allocator);
    json.AddMember("enabled", characterItem.enabled, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* CharacterMovementComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    CharacterSettings settings;
    for (const auto& field : CharacterFields)
        if (!Float(json, field.name, settings.*field.field, error)) return nullptr;
    bool enabled;
    if (!Bool(json, "enabled", enabled, error)) return nullptr;
    if (!settings.Validate(error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) CharacterMovementComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->characterItem.settings = settings;
    result->characterItem.enabled = enabled;
    return result;
}

bool CharacterMovementComponent::ApplyDescription(const rapidjson::Value& json, std::string& error)
{
    auto* description = static_cast<CharacterMovementComponent*>(CreateFromJson(json, error));
    if (!description) return false;
    // Временное описание не проходит Init; живой компонент сохраняет регистрацию и оболочку.
    characterItem.settings = description->characterItem.settings;
    characterItem.enabled = description->characterItem.enabled;
    delete description;
    return true;
}
