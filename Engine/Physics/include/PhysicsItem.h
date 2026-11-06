#pragma once
#include "GGMath.h"
#include <cstdint>

struct TransformNode;
class PhysicsServer;

enum class PhysicsBodyType { Static, Dynamic };

struct PhysicsMaterial {
    float staticFriction = 0.6f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;
};

// Компонент владеет записью; сервер заимствует её только между Init и Shutdown.
struct ColliderItem {
    TransformNode* transformNode = nullptr;
    DXVector3 size = DXVector3::One; // Полные локальные размеры box.
    PhysicsMaterial material;
    bool enabled = true;

    ColliderItem() = default;
    ~ColliderItem();
    ColliderItem(const ColliderItem&) = delete;
    ColliderItem& operator=(const ColliderItem&) = delete;
    bool isRegistered() const { return server != nullptr; }

private:
    friend class PhysicsServer;
    PhysicsServer* server = nullptr;
    // Снимок нужен для пересоздания формы после редактирования параметров.
    DXVector3 appliedSize = DXVector3::Zero;
    PhysicsMaterial appliedMaterial;
    bool appliedEnabled = false;
};

// Актёр относится к записи тела; создаёт и освобождает его только PhysicsServer.
struct PhysicsItem {
    TransformNode* transformNode = nullptr;
    PhysicsBodyType type = PhysicsBodyType::Dynamic;
    float mass = 1.0f;
    bool enabled = true;
    // Сервер читает заданные скорости и обновляет их после завершения шага.
    DXVector3 linearVelocity = DXVector3::Zero;
    DXVector3 angularVelocity = DXVector3::Zero;

    PhysicsItem() = default;
    ~PhysicsItem();
    PhysicsItem(const PhysicsItem&) = delete;
    PhysicsItem& operator=(const PhysicsItem&) = delete;
    bool isRegistered() const { return server != nullptr; }
    bool hasActor() const { return bodyId != InvalidBodyId; }
    bool isSleeping() const { return sleeping; }

private:
    friend class PhysicsServer;
    PhysicsServer* server = nullptr;
    // SDK хранит тело; Item содержит только ID с номером поколения, без заголовков Jolt.
    static constexpr std::uint32_t InvalidBodyId = 0xffffffffu;
    std::uint32_t bodyId = InvalidBodyId;
    bool rebuild = true;
    bool sleeping = false;
    PhysicsBodyType appliedType = PhysicsBodyType::Dynamic;
    float appliedMass = 0.0f;
    DXVector3 appliedScale = DXVector3::Zero;
    // Отличаем изменение узла редактором от результата предыдущей симуляции.
    DXVector3 lastPosition = DXVector3::Zero;
    DXQuaternion lastRotation = DXQuaternion::Identity;
};
