// Jolt подключается до Windows/SimpleMath и остальных заголовков SDK.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include "PhysicsServer.h"
#include "TransformTree.h"
#include "Object.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace {
    // Factory общая для всех одновременно живущих Game; системы владеют ею совместно.
    std::mutex sdkMutex;
    unsigned sdkUsers = 0;
    bool acquireSdk() {
        const std::lock_guard<std::mutex> guard(sdkMutex);
        if (!sdkUsers) {
            if (JPH::Factory::sInstance) return false;
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory;
            JPH::RegisterTypes();
        }
        ++sdkUsers;
        return true;
    }
    void releaseSdk() {
        const std::lock_guard<std::mutex> guard(sdkMutex);
        if (--sdkUsers) return;
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
    bool finite(const DXVector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    float dimension(float value) {
        return std::isfinite(value) ? std::clamp(std::fabs(value), 0.001f, 10000.0f) : 1.0f;
    }
    DXVector3 dimensions(const DXVector3& value) {
        return {dimension(value.x), dimension(value.y), dimension(value.z)};
    }
    bool validPose(TransformNode* node) {
        if (!node || node->get_parent()) return false;
        // Исправляем числовые данные, но не меняем иерархию трансформаций.
        if (!finite(node->get_position())) node->set_position(DXVector3::Zero);
        auto q = node->get_rotation();
        const float lengthSquared = q.LengthSquared();
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.000001f)
            q = DXQuaternion::Identity;
        else if (std::fabs(lengthSquared - 1.0f) > 0.000001f) q.Normalize();
        node->set_rotation(q);
        return true;
    }
    float bounded(float value, float fallback, float minimum, float maximum) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    }
    void sanitize(ColliderItem& collider) {
        collider.size = dimensions(collider.size);
        // Трение неотрицательно, коэффициент восстановления лежит в [0, 1].
        auto& material = collider.material;
        material.staticFriction = bounded(material.staticFriction, 0.6f, 0.0f, 10000.0f);
        material.dynamicFriction = bounded(material.dynamicFriction, 0.5f, 0.0f, 10000.0f);
        material.restitution = bounded(material.restitution, 0.0f, 0.0f, 1.0f);
    }
    bool sameMaterial(const PhysicsMaterial& a, const PhysicsMaterial& b) {
        return a.staticFriction == b.staticFriction && a.dynamicFriction == b.dynamicFriction &&
            a.restitution == b.restitution;
    }
    JPH::Vec3 vector(const DXVector3& v) { return {v.x, v.y, v.z}; }
    DXVector3 vector(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
    JPH::Quat rotation(const TransformNode& node) {
        const auto& q = node.get_rotation();
        return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
    }

    constexpr JPH::ObjectLayer StaticLayer = 0, DynamicLayer = 1;
    class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
            return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
            return layer == JPH::BroadPhaseLayer(0) ? "Static" : "Dynamic";
        }
#endif
    };
    // Статические тела не сталкиваются друг с другом; динамические сталкиваются со всеми.
    class ObjectPairs final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
            return a == DynamicLayer || b == DynamicLayer;
        }
    };
    class BroadPhasePairs final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
            return a == DynamicLayer || b == JPH::BroadPhaseLayer(1);
        }
    };

    // Форма Jolt владеет снимком материала, без указателей на данные компонентов.
    class SurfaceMaterial final : public JPH::PhysicsMaterial {
    public:
        ::PhysicsMaterial data;
        explicit SurfaceMaterial(const ::PhysicsMaterial& value) : data(value) {}
    };
    class SurfaceContacts final : public JPH::ContactListener {
        static void apply(const JPH::Body& a, const JPH::Body& b,
            const JPH::ContactManifold& contact, JPH::ContactSettings& settings) {
            const auto* first = static_cast<const SurfaceMaterial*>(a.GetShape()->GetMaterial(contact.mSubShapeID1));
            const auto* second = static_cast<const SurfaceMaterial*>(b.GetShape()->GetMaterial(contact.mSubShapeID2));
            // У Jolt один коэффициент контакта: ниже 0.01 м/с используем статическое трение.
            bool sliding = false;
            for (JPH::uint i = 0; i < contact.mRelativeContactPointsOn1.size(); ++i) {
                const auto velocity = b.GetPointVelocity(contact.GetWorldSpaceContactPointOn2(i))
                    - a.GetPointVelocity(contact.GetWorldSpaceContactPointOn1(i));
                const auto tangent = velocity - contact.mWorldSpaceNormal * velocity.Dot(contact.mWorldSpaceNormal);
                if (tangent.LengthSq() > 0.0001f) { sliding = true; break; }
            }
            const auto& ma = first->data;
            const auto& mb = second->data;
            settings.mCombinedFriction = 0.5f * (sliding
                ? ma.dynamicFriction + mb.dynamicFriction : ma.staticFriction + mb.staticFriction);
            settings.mCombinedRestitution = 0.5f * (ma.restitution + mb.restitution);
        }
    public:
        void OnContactAdded(const JPH::Body& a, const JPH::Body& b,
            const JPH::ContactManifold& contact, JPH::ContactSettings& settings) override {
            apply(a, b, contact, settings);
        }
        void OnContactPersisted(const JPH::Body& a, const JPH::Body& b,
            const JPH::ContactManifold& contact, JPH::ContactSettings& settings) override {
            apply(a, b, contact, settings);
        }
    };

    // Внутреннее тело меньше внешней сферы, как рекомендует Jolt для CharacterVirtual.
    constexpr float InnerBodyFraction = 0.9f;
    // Только SDK Ref владеет персонажем Jolt: Item хранит настройки и не видит типы Jolt.
    struct CharacterActor {
        CharacterItem* item = nullptr;
        JPH::Ref<JPH::CharacterVirtual> character;
        CharacterSettings applied;
    };
}

struct PhysicsServer::Impl {
    // Эти объекты живут дольше PhysicsSystem, который заимствует их адреса.
    BroadPhaseLayers layers;
    ObjectPairs objectPairs;
    BroadPhasePairs broadPhasePairs;
    SurfaceContacts contacts;
    JPH::TempAllocatorMalloc allocator;
    JPH::JobSystemSingleThreaded jobs;
    JPH::PhysicsSystem scene;
    // Оба списка заимствуют адреса: владельцами записей остаются компоненты.
    std::vector<PhysicsItem*> items;
    std::vector<ColliderItem*> colliders;
    // Объявлен после scene: персонаж удаляет внутреннее тело, пока PhysicsSystem жив.
    std::vector<CharacterActor> characters;
    std::vector<TriggerItem*> triggers;
};

bool PhysicsItem::accumulate(DXVector3& pending, const DXVector3& value)
{
    if (!enabled || type != PhysicsBodyType::Dynamic || !finite(value)) return false;
    const DXVector3 total = pending + value;
    // При переполнении оставляем ранее принятые команды без изменений.
    if (!finite(total)) return false;
    pending = total;
    return true;
}

bool PhysicsItem::addForce(const DXVector3& force) { return accumulate(pendingForce, force); }
bool PhysicsItem::addImpulse(const DXVector3& impulse) { return accumulate(pendingImpulse, impulse); }
bool PhysicsItem::addTorque(const DXVector3& torque) { return accumulate(pendingTorque, torque); }
bool PhysicsItem::addAngularImpulse(const DXVector3& impulse) { return accumulate(pendingAngularImpulse, impulse); }

void PhysicsItem::clearPendingActions()
{
    pendingForce = pendingImpulse = DXVector3::Zero;
    pendingTorque = pendingAngularImpulse = DXVector3::Zero;
}

PhysicsItem::~PhysicsItem() { if (server) server->removeItem(*this); }
ColliderItem::~ColliderItem() { if (server) server->removeCollider(*this); }
CharacterItem::~CharacterItem() { if (server) server->removeCharacter(*this); }

bool CharacterItem::move(const DXVector3& desired)
{
    if (!enabled || !finite(desired)) return false;
    desiredVelocity = desired;
    return true;
}

bool CharacterSettings::Validate(std::string& error) const
{
    const float values[] = { radius, gravityScale, maxSlope, stepHeight, mass, maxStrength };
    for (float value : values)
        if (!std::isfinite(value)) { error = "Character settings must be finite numbers"; return false; }
    if (radius < 0.01f || radius > 100.0f) { error = "Character radius must be in [0.01, 100]"; return false; }
    if (gravityScale < 0.0f || gravityScale > 100.0f) { error = "Gravity scale must be in [0, 100]"; return false; }
    if (maxSlope < 0.0f || maxSlope > 89.0f) { error = "Max slope must be in [0, 89] degrees"; return false; }
    if (stepHeight < 0.0f || stepHeight > 10.0f) { error = "Step height must be in [0, 10]"; return false; }
    if (mass < 0.001f || mass > 1000000.0f) { error = "Character mass must be in [0.001, 1000000]"; return false; }
    if (maxStrength < 0.0f || maxStrength > 1000000000.0f) { error = "Max strength must be in [0, 1000000000]"; return false; }
    error.clear();
    return true;
}
TriggerItem::~TriggerItem() { if (server) server->removeTrigger(*this); }
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
    if (!acquireSdk()) return reportError("Jolt factory is owned by another integration");
    auto* created = new (std::nothrow) Impl;
    if (!created) { releaseSdk(); return reportError("cannot allocate Jolt state"); }

    // Задачи выполняются синхронно, без отдельного потока и второго игрового цикла.
    created->jobs.Init(JPH::cMaxPhysicsJobs);
    created->scene.Init(8192, 0, 65536, 16384, created->layers, created->broadPhasePairs, created->objectPairs);
    created->scene.SetGravity(vector(finite(gravity) ? gravity : DXVector3(0, -9.81f, 0)));
    created->scene.SetContactListener(&created->contacts);
    impl = created;
    return true;
}

void PhysicsServer::releaseActor(PhysicsItem& item)
{
    if (item.hasActor()) {
        // Сначала удаляем из broadphase, затем освобождаем тело и его ссылки на формы.
        auto& bodies = impl->scene.GetBodyInterface();
        const JPH::BodyID id(item.bodyId);
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    item.bodyId = PhysicsItem::InvalidBodyId;
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
    for (const CharacterActor& actor : impl->characters)
        if (actor.item->transformNode == item.transformNode)
            return reportError("a character cannot share its root transform with a rigid body");
    // Регистрация возможна до коллайдеров: актёр собирается перед fixed-step.
    impl->items.push_back(&item);
    item.server = this;
    item.rebuild = true;
    return true;
}

void PhysicsServer::removeItem(PhysicsItem& item)
{
    if (item.server != this) return;
    // Удаление тела не отправляет Lua ссылку на уже уничтожаемый объект.
    for (auto* trigger : impl->triggers) {
        auto& overlaps = trigger->overlaps;
        overlaps.erase(std::remove(overlaps.begin(), overlaps.end(), &item), overlaps.end());
    }
    releaseActor(item);
    // Команды старой сцены не должны переживать Shutdown/повторный Init компонента.
    item.clearPendingActions();
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

bool PhysicsServer::addCharacter(CharacterItem& item)
{
    if (item.server) return item.server == this;
    if (!validPose(item.transformNode)) return reportError("character needs a finite root pose and unit quaternion");
    if (!Init()) return false;
    for (const PhysicsItem* registered : impl->items)
        if (registered->transformNode == item.transformNode)
            return reportError("a character cannot share its root transform with a rigid body");
    for (const CharacterActor& actor : impl->characters)
        if (actor.item->transformNode == item.transformNode)
            return reportError("only one character per root transform is supported");
    // Объект Jolt создаётся перед fixed-step, как и актёры тел.
    CharacterActor actor;
    actor.item = &item;
    impl->characters.push_back(std::move(actor));
    item.server = this;
    return true;
}

void PhysicsServer::removeCharacter(CharacterItem& item)
{
    if (item.server != this) return;
    auto& characters = impl->characters;
    // Удаление записи освобождает персонажа и его внутреннее тело.
    characters.erase(std::remove_if(characters.begin(), characters.end(),
        [&](const CharacterActor& actor) { return actor.item == &item; }), characters.end());
    item.server = nullptr;
    item.velocity = DXVector3::Zero;
    item.grounded = false;
}

void PhysicsServer::Shutdown()
{
    if (impl) {
        // Пережившие систему Item остаются валидными и больше не обращаются к ней.
        while (!impl->triggers.empty()) removeTrigger(*impl->triggers.back());
        while (!impl->items.empty()) removeItem(*impl->items.back());
        while (!impl->colliders.empty()) removeCollider(*impl->colliders.back());
        while (!impl->characters.empty()) removeCharacter(*impl->characters.back().item);
        delete impl;
        impl = nullptr;
        releaseSdk(); // Factory удаляется после последнего PhysicsSystem и всех его форм.
    }
    lastError.clear();
}

bool PhysicsServer::addTrigger(TriggerItem& item)
{
    if (item.server) return item.server == this;
    if (!item.component || !validPose(item.transformNode))
        return reportError("box trigger needs a component and root transform");
    if (!Init()) return false;
    impl->triggers.push_back(&item);
    item.server = this;
    item.overlaps.clear();
    return true;
}

void PhysicsServer::removeTrigger(TriggerItem& item)
{
    if (item.server != this) return;
    auto& triggers = impl->triggers;
    triggers.erase(std::remove(triggers.begin(), triggers.end(), &item), triggers.end());
    item.overlaps.clear();
    item.server = nullptr;
}

bool PhysicsServer::updateTriggers()
{
    if (impl->triggers.empty()) return true;
    struct Event { TriggerItem* trigger; PhysicsItem* body; bool entered; };
    std::vector<Event> events;
    // Номер поколения BodyID предотвращает смешение старого тела с новым.
    std::unordered_map<std::uint32_t, PhysicsItem*> bodies;
    for (auto* item : impl->items)
        if (item->hasActor()) bodies.emplace(item->bodyId, item);

    for (auto* trigger : impl->triggers) {
        auto* owner = trigger->component->getOwner();
        if (!owner || owner->isPendingDestroy() || trigger->component->isPendingDestroy()) continue;
        std::vector<PhysicsItem*> current;
        if (trigger->enabled) {
            if (!validPose(trigger->transformNode)) return reportError("box trigger needs a root transform");
            trigger->size = dimensions(trigger->size);
            const auto half = dimensions(trigger->size * dimensions(trigger->transformNode->get_scale()) * 0.5f);
            // Запрос не создаёт тело и не участвует в разрешении столкновений.
            JPH::BoxShape shape(vector(half), (std::min)({0.05f, half.x, half.y, half.z}) * 0.1f);
            const JPH::RVec3 position(vector(trigger->transformNode->get_position()));
            const auto pose = JPH::RMat44::sRotationTranslation(rotation(*trigger->transformNode), position);
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            impl->scene.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sReplicate(1.0f),
                pose, JPH::CollideShapeSettings{}, position, collector);
            for (const auto& hit : collector.mHits) {
                const auto found = bodies.find(hit.mBodyID2.GetIndexAndSequenceNumber());
                if (found == bodies.end()) continue;
                auto* body = found->second;
                auto* other = body->transformNode->getOwner();
                if (!other || other == owner || other->isPendingDestroy()) continue;
                // Несколько форм одного тела дают один вход и один выход.
                if (std::find(current.begin(), current.end(), body) == current.end()) current.push_back(body);
            }
        }
        for (auto* body : trigger->overlaps)
            if (std::find(current.begin(), current.end(), body) == current.end()) events.push_back({trigger, body, false});
        for (auto* body : current)
            if (std::find(trigger->overlaps.begin(), trigger->overlaps.end(), body) == trigger->overlaps.end())
                events.push_back({trigger, body, true});
        trigger->overlaps = std::move(current);
    }

    // Все запросы завершены и состояние обновлено до первого пользовательского callback.
    for (const auto& event : events) {
        if (std::find(impl->triggers.begin(), impl->triggers.end(), event.trigger) == impl->triggers.end()
            || std::find(impl->items.begin(), impl->items.end(), event.body) == impl->items.end()) continue;
        auto* owner = event.trigger->component->getOwner();
        auto* other = event.body->transformNode->getOwner();
        if (!owner || !other || owner->isPendingDestroy() || other->isPendingDestroy()
            || event.trigger->component->isPendingDestroy()) continue;
        (event.entered ? event.trigger->bodyEntered : event.trigger->bodyExited).Broadcast(other);
    }
    return true;
}

bool PhysicsServer::rebuildActor(PhysicsItem& item)
{
    releaseActor(item);
    // Отражение графики не меняем: физическая коробка использует модуль масштаба.
    item.appliedScale = dimensions(item.transformNode->get_scale());
    item.appliedType = item.type;
    item.appliedMass = item.mass;
    item.rebuild = false;

    JPH::RefConst<JPH::Shape> shape;
    JPH::StaticCompoundShapeSettings compound;
    unsigned count = 0;
    for (const ColliderItem* collider : impl->colliders) {
        if (!collider->enabled || collider->transformNode != item.transformNode) continue;
        const DXVector3 half = dimensions(collider->size * item.appliedScale * 0.5f);
        // Только SDK Ref: форма удерживает материал, затем тело удерживает форму.
        const JPH::RefConst<SurfaceMaterial> material = new SurfaceMaterial(collider->material);
        JPH::BoxShapeSettings box(vector(half), (std::min)({0.05f, half.x, half.y, half.z}) * 0.1f, material);
        const auto result = box.Create();
        if (result.HasError()) return reportError(result.GetError().c_str());
        shape = result.Get();
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape);
        ++count;
    }
    if (!count) return true; // Без активного коллайдера тело не создаётся.
    if (count > 1) {
        const auto result = compound.Create();
        if (result.HasError()) return reportError(result.GetError().c_str());
        shape = result.Get();
    }

    const bool dynamic = item.type == PhysicsBodyType::Dynamic;
    // Кинематическое тело использует тот же слой столкновений, что и динамическое.
    const bool moving = item.type != PhysicsBodyType::Static;
    JPH::BodyCreationSettings settings(shape, JPH::RVec3(vector(item.transformNode->get_position())),
        rotation(*item.transformNode),
        dynamic ? JPH::EMotionType::Dynamic : moving ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
        moving ? DynamicLayer : StaticLayer);
    if (dynamic) {
        // Инерция вычисляется по геометрии; полную массу задаёт компонент.
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = item.mass;
        settings.mLinearDamping = 0.0f;
    }
    auto& bodies = impl->scene.GetBodyInterface();
    const auto id = bodies.CreateAndAddBody(settings,
        moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (id.IsInvalid()) return reportError("cannot create Jolt body: body capacity exceeded");
    item.bodyId = id.GetIndexAndSequenceNumber();
    if (moving) {
        // CreationSettings требует уже ограниченные скорости; BodyInterface ограничивает их сам.
        bodies.SetLinearAndAngularVelocity(id, vector(item.linearVelocity), vector(item.angularVelocity));
        JPH::Vec3 linear, angular;
        bodies.GetLinearAndAngularVelocity(id, linear, angular);
        item.linearVelocity = vector(linear);
        item.angularVelocity = vector(angular);
    }
    item.lastPosition = item.transformNode->get_position();
    item.lastRotation = item.transformNode->get_rotation();
    return true;
}

bool PhysicsServer::Update(double fixedInterval)
{
    if (!lastError.empty()) return false;
    if (!impl) return true; // В обычных сценах SDK не создаётся без физических компонентов.
    // Невалидный шаг заменяем стандартными 60 Гц.
    const float dt = std::isfinite(fixedInterval) && fixedInterval > 0 && fixedInterval <= 1
        ? (std::max)(static_cast<float>(fixedInterval), 0.000001f) : 1.0f / 60.0f;

    for (ColliderItem* collider : impl->colliders) {
        if (collider->enabled && !validPose(collider->transformNode))
            return reportError("box collider needs a root transform");
        sanitize(*collider);
        // Изменения данных компонента требуют обновить форму, а не графический ресурс.
        if (collider->size != collider->appliedSize || collider->enabled != collider->appliedEnabled ||
            !sameMaterial(collider->material, collider->appliedMaterial)) invalidateNode(collider->transformNode);
        collider->appliedSize = collider->size;
        collider->appliedMaterial = collider->material;
        collider->appliedEnabled = collider->enabled;
    }
    auto& bodies = impl->scene.GetBodyInterface();
    for (PhysicsItem* item : impl->items) {
        if (!item->enabled) {
            releaseActor(*item);
            item->clearPendingActions();
            continue;
        }
        if (!validPose(item->transformNode)) return reportError("body needs a root transform");
        // Повреждённые скорости сбрасываем, массу ограничиваем рабочим диапазоном.
        if (!finite(item->linearVelocity)) item->linearVelocity = DXVector3::Zero;
        if (!finite(item->angularVelocity)) item->angularVelocity = DXVector3::Zero;
        item->mass = bounded(item->mass, 1.0f, 0.001f, 1000000.0f);
        if (item->type != PhysicsBodyType::Static && item->type != PhysicsBodyType::Dynamic &&
            item->type != PhysicsBodyType::Kinematic)
            item->type = PhysicsBodyType::Dynamic;
        if (item->rebuild || item->type != item->appliedType || item->mass != item->appliedMass ||
            dimensions(item->transformNode->get_scale()) != item->appliedScale)
            if (!rebuildActor(*item)) return false;
        if (!item->hasActor()) {
            // Нет коллайдера: не откладываем старый толчок до его появления.
            item->clearPendingActions();
            continue;
        }

        const JPH::BodyID id(item->bodyId);
        const bool dynamic = item->type == PhysicsBodyType::Dynamic;
        const bool kinematic = item->type == PhysicsBodyType::Kinematic;
        // Gizmo/Lua передают позу один раз; результат предыдущего шага не телепортируем.
        const bool poseChanged = item->transformNode->get_position() != item->lastPosition ||
            item->transformNode->get_rotation() != item->lastRotation;
        if (poseChanged && kinematic) {
            // Jolt вычисляет скорость до новой позы, поэтому динамические тела расталкиваются.
            bodies.MoveKinematic(id, JPH::RVec3(vector(item->transformNode->get_position())),
                rotation(*item->transformNode), dt);
        } else if (poseChanged) {
            // Допуск Jolt отсекает численный шум без лишнего пробуждения и обновления broadphase.
            bodies.SetPositionAndRotationWhenChanged(id, JPH::RVec3(vector(item->transformNode->get_position())),
                rotation(*item->transformNode), dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        } else if (kinematic) {
            // Без переноса узла тело движется заданными скоростями; нулевые его останавливают.
            bodies.SetLinearAndAngularVelocity(id, vector(item->linearVelocity), vector(item->angularVelocity));
        }
        if (dynamic) {
            JPH::Vec3 linear, angular;
            bodies.GetLinearAndAngularVelocity(id, linear, angular);
            // Не будим уснувшие тела, если скорости Item не менялись извне.
            if (vector(linear) != item->linearVelocity || vector(angular) != item->angularVelocity)
                bodies.SetLinearAndAngularVelocity(id, vector(item->linearVelocity), vector(item->angularVelocity));

            // После синхронизации скоростей: иначе старый Item затрёт результат импульса.
            if (item->pendingImpulse != DXVector3::Zero)
                bodies.AddImpulse(id, vector(item->pendingImpulse));
            if (item->pendingAngularImpulse != DXVector3::Zero)
                bodies.AddAngularImpulse(id, vector(item->pendingAngularImpulse));
            // Jolt сам учитывает массу, инерцию и dt; ненулевое воздействие будит тело.
            if (item->pendingForce != DXVector3::Zero || item->pendingTorque != DXVector3::Zero)
                bodies.AddForceAndTorque(id, vector(item->pendingForce), vector(item->pendingTorque));
        }
        // Следующий fixed-step не повторяет команды, даже если шагов в кадре несколько.
        item->clearPendingActions();
    }
    if (!stepCharacters(dt)) return false;
    const auto error = impl->scene.Update(dt, 1, &impl->allocator, &impl->jobs);
    if (error != JPH::EPhysicsUpdateError::None) return reportError("Jolt update capacity exceeded");
    for (PhysicsItem* item : impl->items) {
        if (!item->hasActor()) continue;
        if (item->type == PhysicsBodyType::Dynamic) {
            const JPH::BodyID id(item->bodyId);
            JPH::RVec3 position;
            JPH::Quat orientation;
            bodies.GetPositionAndRotation(id, position, orientation);
            item->transformNode->set_position({static_cast<float>(position.GetX()),
                static_cast<float>(position.GetY()), static_cast<float>(position.GetZ())});
            item->transformNode->set_rotation({orientation.GetX(), orientation.GetY(), orientation.GetZ(), orientation.GetW()});
            // Масштаб узла не меняем; скорости и сон — актуальные данные Item.
            JPH::Vec3 linear, angular;
            bodies.GetLinearAndAngularVelocity(id, linear, angular);
            item->linearVelocity = vector(linear);
            item->angularVelocity = vector(angular);
            item->sleeping = !bodies.IsActive(id);
        }
        if (item->type == PhysicsBodyType::Kinematic) {
            // Скорости Item остаются командой пользователя, в узел возвращается только поза.
            const JPH::BodyID id(item->bodyId);
            JPH::RVec3 position;
            JPH::Quat orientation;
            bodies.GetPositionAndRotation(id, position, orientation);
            item->transformNode->set_position({static_cast<float>(position.GetX()),
                static_cast<float>(position.GetY()), static_cast<float>(position.GetZ())});
            item->transformNode->set_rotation({orientation.GetX(), orientation.GetY(), orientation.GetZ(), orientation.GetW()});
            item->sleeping = !bodies.IsActive(id);
        }
        item->lastPosition = item->transformNode->get_position();
        item->lastRotation = item->transformNode->get_rotation();
    }
    return updateTriggers();
}

bool PhysicsServer::stepCharacters(float dt)
{
    // Персонаж проверяет столкновения как подвижное тело: со статикой и всеми телами.
    const auto broadPhaseFilter = impl->scene.GetDefaultBroadPhaseLayerFilter(DynamicLayer);
    const auto layerFilter = impl->scene.GetDefaultLayerFilter(DynamicLayer);
    const JPH::BodyFilter bodyFilter;
    const JPH::ShapeFilter shapeFilter;
    for (CharacterActor& actor : impl->characters) {
        CharacterItem& item = *actor.item;
        if (!item.enabled) {
            // Выключенный персонаж убирает внутреннее тело из мира.
            actor.character = nullptr;
            item.velocity = DXVector3::Zero;
            item.grounded = false;
            continue;
        }
        if (!validPose(item.transformNode)) return reportError("character needs a root transform");
        TransformNode& node = *item.transformNode;
        const CharacterSettings& settings = item.settings;
        const CharacterSettings& applied = actor.applied;
        // Гравитация и высота ступени читаются каждый шаг, остальное задаётся при создании.
        if (!actor.character || settings.radius != applied.radius || settings.maxSlope != applied.maxSlope ||
            settings.mass != applied.mass || settings.maxStrength != applied.maxStrength) {
            const JPH::Vec3 keptVelocity = actor.character ? actor.character->GetLinearVelocity() : JPH::Vec3::sZero();
            actor.character = nullptr;
            // Контакты приводят материал к SurfaceMaterial, поэтому другой тип здесь недопустим.
            const JPH::RefConst<SurfaceMaterial> material = new SurfaceMaterial(::PhysicsMaterial{});
            // TODO: заменить сферу на капсулу с настройкой высоты.
            JPH::SphereShapeSettings outer(settings.radius, material);
            JPH::SphereShapeSettings inner(settings.radius * InnerBodyFraction, material);
            const auto outerShape = outer.Create();
            if (outerShape.HasError()) return reportError(outerShape.GetError().c_str());
            const auto innerShape = inner.Create();
            if (innerShape.HasError()) return reportError(innerShape.GetError().c_str());
            JPH::Ref<JPH::CharacterVirtualSettings> created = new JPH::CharacterVirtualSettings;
            created->mShape = outerShape.Get();
            created->mInnerBodyShape = innerShape.Get();
            created->mInnerBodyLayer = DynamicLayer;
            created->mMaxSlopeAngle = JPH::DegreesToRadians(settings.maxSlope);
            created->mMass = settings.mass;
            created->mMaxStrength = settings.maxStrength;
            // Опорой считаются только контакты нижней половины сферы.
            created->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), 0.0f);
            // TODO: добавить альтернативную реализацию на JPH::Character (твёрдое тело).
            actor.character = new JPH::CharacterVirtual(created, JPH::RVec3(vector(node.get_position())),
                JPH::Quat::sIdentity(), &impl->scene);
            actor.character->SetLinearVelocity(keptVelocity);
            actor.applied = settings;
            item.lastPosition = node.get_position();
        }
        JPH::CharacterVirtual& character = *actor.character;
        // Перенос узла скриптом или редактором телепортирует персонажа и гасит скорость.
        if (node.get_position() != item.lastPosition) {
            character.SetPosition(JPH::RVec3(vector(node.get_position())));
            character.SetLinearVelocity(JPH::Vec3::sZero());
        }
        character.UpdateGroundVelocity();
        const JPH::Vec3 up = character.GetUp();
        const JPH::Vec3 vertical = up * character.GetLinearVelocity().Dot(up);
        const JPH::Vec3 ground = character.GetGroundVelocity();
        const JPH::Vec3 gravity = impl->scene.GetGravity() * settings.gravityScale;
        // На земле наследуем её скорость, в воздухе сохраняем вертикальную и добавляем гравитацию.
        const bool standing = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround &&
            (vertical - ground).Dot(up) < 0.1f;
        const JPH::Vec3 base = (standing ? ground : vertical) + gravity * dt;
        // Команда действует только по XZ и не толкает персонажа на слишком крутой склон.
        const JPH::Vec3 desired(item.desiredVelocity.x, 0.0f, item.desiredVelocity.z);
        character.SetLinearVelocity(base + character.CancelVelocityTowardsSteepSlopes(desired));

        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp = up * settings.stepHeight;
        update.mStickToFloorStepDown = -up * (std::max)(settings.stepHeight, 0.1f);
        character.ExtendedUpdate(dt, gravity, update, broadPhaseFilter, layerFilter, bodyFilter, shapeFilter, impl->allocator);

        const JPH::RVec3 position = character.GetPosition();
        node.set_position({static_cast<float>(position.GetX()),
            static_cast<float>(position.GetY()), static_cast<float>(position.GetZ())});
        item.lastPosition = node.get_position();
        item.velocity = vector(character.GetLinearVelocity());
        item.grounded = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }
    return true;
}
