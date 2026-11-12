#include "NavAgentComponent.h"
#include "core.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <new>
#include <utility>

namespace {
    // Радиус достижения промежуточного угла, минимальная скорость подхода к цели.
    constexpr float CornerRadius = 0.15f;
    constexpr float MinApproachSpeed = 0.25f;
    constexpr float MinStoppingDistance = 0.05f;
    // Запас, при котором частичный путь ещё считается продвижением к цели.
    constexpr float PartialProgressMargin = 0.25f;
    // Застревание и отклонение от пути приводят к перестроению.
    constexpr float StuckSeconds = 0.5f;
    constexpr float MaxPathDeviation = 1.0f;
    // Зазор над навмешем при мгновенном переносе, чтобы сфера не начала в полу.
    constexpr float WarpClearance = 0.05f;
    constexpr float TwoPi = 6.28318530718f;
    const char* const NoCharacterError = "Nav agent needs a CharacterMovementComponent on the same object";

    struct AgentField {
        const char* name;
        float NavAgentSettings::* field;
    };
    constexpr AgentField AgentFields[] = {
        { "speed", &NavAgentSettings::speed },
        { "acceleration", &NavAgentSettings::acceleration },
        { "angular_speed", &NavAgentSettings::angularSpeed },
        { "stopping_distance", &NavAgentSettings::stoppingDistance },
    };

    float PlanarDistance(const DXVector3& a, const DXVector3& b)
    {
        return std::hypot(a.x - b.x, a.z - b.z);
    }

    // Расстояние по XZ от точки до отрезка текущего участка пути.
    float PlanarDistanceToSegment(const DXVector3& point, const DXVector3& a, const DXVector3& b)
    {
        const float dx = b.x - a.x, dz = b.z - a.z;
        const float lengthSquared = dx * dx + dz * dz;
        if (lengthSquared < 0.000001f) return PlanarDistance(point, a);
        const float t = std::clamp(((point.x - a.x) * dx + (point.z - a.z) * dz) / lengthSquared, 0.0f, 1.0f);
        return std::hypot(point.x - (a.x + dx * t), point.z - (a.z + dz * t));
    }
}

bool NavAgentSettings::Validate(std::string& error) const
{
    const float values[] = { speed, acceleration, angularSpeed, stoppingDistance };
    for (float value : values)
        if (!std::isfinite(value)) { error = "Nav agent settings must be finite numbers"; return false; }
    if (speed <= 0.0f || speed > 1000.0f) { error = "Agent speed must be in (0, 1000]"; return false; }
    if (acceleration < 0.01f || acceleration > 10000.0f) { error = "Agent acceleration must be in [0.01, 10000]"; return false; }
    if (angularSpeed < 0.0f || angularSpeed > 100000.0f) { error = "Agent angular speed must be in [0, 100000]"; return false; }
    if (stoppingDistance < 0.0f || stoppingDistance > 100.0f) { error = "Stopping distance must be in [0, 100]"; return false; }
    error.clear();
    return true;
}

CharacterMovementComponent* NavAgentComponent::findCharacter() const
{
    if (!getOwner()) return nullptr;
    // Поиск при каждом обращении: компонент персонажа могут удалить в любой момент.
    for (Component* component : getOwner()->getComponents()) {
        if (!component || component->isPendingDestroy()) continue;
        if (auto* character = dynamic_cast<CharacterMovementComponent*>(component)) return character;
    }
    return nullptr;
}

bool NavAgentComponent::setSettings(const NavAgentSettings& settings, std::string* error)
{
    std::string reason;
    if (!settings.Validate(reason)) {
        if (error) *error = reason;
        return false;
    }
    agentItem.settings = settings;
    return true;
}

void NavAgentComponent::halt(NavAgentState state)
{
    auto& item = agentItem;
    if (CharacterMovementComponent* character = findCharacter()) character->move(DXVector3::Zero);
    item.state = state;
    item.path.clear();
    item.nextCorner = 0;
    item.currentSpeed = item.remainingDistance = item.stuckTime = 0.0f;
    item.planPending = false;
}

bool NavAgentComponent::fail(std::string reason)
{
    halt(NavAgentState::Failed);
    agentItem.lastError = std::move(reason);
    std::cout << "[Navigation] " << agentItem.lastError << std::endl;
    return false;
}

bool NavAgentComponent::plan()
{
    auto& item = agentItem;
    NavigationServer& navigation = activeGame->getNavigationServer();
    std::vector<DXVector3> corners;
    bool partial = false;
    if (!navigation.FindPath(transformNode->get_position(), item.destination, corners, &partial) || corners.empty())
        return fail(navigation.getLastError().empty() ? std::string("Path not found") : navigation.getLastError());
    item.path = std::move(corners);
    // Первый угол — проекция текущей позиции, идти нужно к следующему.
    item.nextCorner = item.path.size() > 1 ? 1 : 0;
    item.partial = partial;
    item.state = NavAgentState::Moving;
    item.stuckTime = 0.0f;
    item.lastPosition = transformNode->get_position();
    item.lastError.clear();
    return true;
}

bool NavAgentComponent::setDestination(const DXVector3& target)
{
    auto& item = agentItem;
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
        return fail("Invalid nav agent destination");
    item.destination = target;
    item.replanned = false;
    // В on_init навмеш или персонаж могут быть ещё не готовы: путь строится в первом кадре.
    if (!item.started) {
        item.planPending = true;
        item.state = NavAgentState::Moving;
        item.lastError.clear();
        return true;
    }
    if (!activeGame || !transformNode) return fail("Nav agent is not initialized");
    if (!findCharacter()) return fail(NoCharacterError);
    return plan();
}

void NavAgentComponent::stop()
{
    halt(NavAgentState::Idle);
}

bool NavAgentComponent::warp(const DXVector3& point)
{
    if (!activeGame || !transformNode) return fail("Nav agent is not initialized");
    const CharacterMovementComponent* character = findCharacter();
    if (!character) return fail(NoCharacterError);
    NavigationServer& navigation = activeGame->getNavigationServer();
    DXVector3 surface;
    if (!navigation.ProjectPoint(point, surface)) return fail(navigation.getLastError());
    halt(NavAgentState::Idle);
    // Центр сферы ставим над поверхностью; физика заметит перенос узла на своём шаге.
    transformNode->set_position(surface + DXVector3(0.0f, character->getSettings().radius + WarpClearance, 0.0f));
    return true;
}

DXVector3 NavAgentComponent::getVelocity() const
{
    const CharacterMovementComponent* character = findCharacter();
    return character ? DXVector3(character->getCharacterItem().velocity) : DXVector3::Zero;
}

void NavAgentComponent::Init(Game& game)
{
    TransformNode* root = getOwner() ? &getOwner()->getTransform() : nullptr;
    if (!transformNode) bind_to_node(root);
    activeGame = &game;
    agentItem.transformNode = transformNode;
    // Ошибка не фатальна: без корня агент остаётся неподвижным, игра продолжается.
    if (!root || transformNode != root) fail("Nav agent must use its owner's root transform");
}

void NavAgentComponent::Shutdown(Game& game)
{
    halt(NavAgentState::Idle);
    agentItem.transformNode = nullptr;
    activeGame = nullptr;
}

void NavAgentComponent::Update(Game& game, double deltaTime)
{
    // В редакторе сцена не симулируется, а поля настройки не должны двигать объект.
    if (game.isEditorMode() || !activeGame || !transformNode) return;
    auto& item = agentItem;
    item.started = true;
    if (item.state != NavAgentState::Moving) return;
    CharacterMovementComponent* character = findCharacter();
    if (!character) { fail(NoCharacterError); return; }
    if (item.planPending) {
        item.planPending = false;
        if (!plan()) return;
    }
    const float dt = static_cast<float>(deltaTime);
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    const NavAgentSettings& settings = item.settings;
    const DXVector3 position = transformNode->get_position();

    // Кадры без физического шага сбрасывают счётчик на следующем кадре с движением.
    const float moved = PlanarDistance(position, item.lastPosition);
    item.lastPosition = position;
    if (item.currentSpeed > MinApproachSpeed * 0.5f && moved < item.currentSpeed * dt * 0.2f) item.stuckTime += dt;
    else item.stuckTime = 0.0f;
    if (item.stuckTime >= StuckSeconds) {
        if (item.replanned) { fail("Nav agent is stuck"); return; }
        if (!plan()) return;
        item.replanned = true;
    }

    const float reach = (std::max)(CornerRadius, item.currentSpeed * dt);
    while (item.nextCorner + 1 < item.path.size() && PlanarDistance(position, item.path[item.nextCorner]) <= reach) {
        ++item.nextCorner;
        item.replanned = false;
    }
    // Персонажа могли оттолкнуть: идти к старому углу по прямой уже небезопасно.
    if (item.nextCorner > 0 && PlanarDistanceToSegment(position,
        item.path[item.nextCorner - 1], item.path[item.nextCorner]) > MaxPathDeviation) {
        if (!plan()) return;
    }

    // Оставшаяся длина пути считается по XZ: высоту задаёт CharacterMovementComponent.
    float remaining = PlanarDistance(position, item.path[item.nextCorner]);
    for (size_t i = item.nextCorner; i + 1 < item.path.size(); ++i)
        remaining += PlanarDistance(item.path[i], item.path[i + 1]);
    item.remainingDistance = remaining;
    const float stopDistance = (std::max)(settings.stoppingDistance, MinStoppingDistance);
    if (remaining <= stopDistance) {
        // Частичный путь продолжаем, пока новый запрос даёт реальное продвижение к цели.
        if (item.partial) {
            if (!plan()) return;
            if (PlanarDistance(position, item.path.back()) > stopDistance + PartialProgressMargin) return;
        }
        const bool partial = item.partial;
        halt(NavAgentState::Arrived);
        item.partial = partial;
        return;
    }

    // Торможение рассчитано так, чтобы скорость упала к точке остановки.
    const float brakingSpeed = std::sqrt(2.0f * settings.acceleration * (std::max)(remaining - stopDistance, 0.0f));
    const float targetSpeed = (std::min)(settings.speed, (std::max)(brakingSpeed, (std::min)(MinApproachSpeed, settings.speed)));
    const float change = settings.acceleration * dt;
    item.currentSpeed += std::clamp(targetSpeed - item.currentSpeed, -change, change);

    DXVector3 direction(item.path[item.nextCorner].x - position.x, 0.0f, item.path[item.nextCorner].z - position.z);
    const float length = direction.Length();
    if (length < 0.0001f) { character->move(DXVector3::Zero); return; }
    direction /= length;
    character->move(direction * item.currentSpeed);

    if (settings.updateRotation) {
        // Вперёд — локальная ось -Z, как у камер и источников света.
        const DXVector3 forward = DXVector3::Transform(DXVector3(0.0f, 0.0f, -1.0f), transformNode->get_rotation());
        const float currentYaw = std::atan2(-forward.x, -forward.z);
        const float targetYaw = std::atan2(-direction.x, -direction.z);
        const float maxStep = DirectX::XMConvertToRadians(settings.angularSpeed) * dt;
        const float yaw = currentYaw + std::clamp(std::remainder(targetYaw - currentYaw, TwoPi), -maxStep, maxStep);
        transformNode->set_rotation(DXQuaternion::CreateFromAxisAngle(DXVector3::UnitY, yaw));
    }
}

rapidjson::Value NavAgentComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // Текущий путь и состояние относятся к симуляции и в сцену не попадают.
    for (const auto& field : AgentFields)
        json.AddMember(rapidjson::StringRef(field.name), agentItem.settings.*field.field, allocator);
    json.AddMember("update_rotation", agentItem.settings.updateRotation, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* NavAgentComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    NavAgentSettings settings;
    for (const auto& field : AgentFields)
        if (!Float(json, field.name, settings.*field.field, error)) return nullptr;
    if (!Bool(json, "update_rotation", settings.updateRotation, error)) return nullptr;
    if (!settings.Validate(error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) NavAgentComponent();
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    result->agentItem.settings = settings;
    return result;
}

bool NavAgentComponent::ApplyDescription(const rapidjson::Value& json, std::string& error)
{
    auto* description = static_cast<NavAgentComponent*>(CreateFromJson(json, error));
    if (!description) return false;
    // Временное описание не проходит Init; живой компонент сохраняет своё состояние.
    agentItem.settings = description->agentItem.settings;
    delete description;
    return true;
}
