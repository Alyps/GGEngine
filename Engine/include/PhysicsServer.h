#pragma once
#include "PhysicsItem.h"
#include <string>

// Game владеет системой, компоненты — Item. PhysX скрыт в реализации.
class PhysicsServer {
    struct Impl;
    Impl* impl = nullptr;
    std::string lastError;
    void releaseActor(PhysicsItem& item);
    void invalidateNode(const TransformNode* node);
    bool rebuildActor(PhysicsItem& item);

public:
    PhysicsServer() = default;
    ~PhysicsServer();
    PhysicsServer(const PhysicsServer&) = delete;
    PhysicsServer& operator=(const PhysicsServer&) = delete;

    bool Init(const DXVector3& gravity = DXVector3(0, -9.81f, 0));
    void Shutdown();
    bool isInitialized() const { return impl != nullptr; }
    const std::string& getLastError() const { return lastError; }
    bool reportError(const char* message);

    // Регистрация не передаёт владение Item и не требует наличия меша.
    bool addItem(PhysicsItem& item);
    void removeItem(PhysicsItem& item);
    bool addCollider(ColliderItem& item);
    void removeCollider(ColliderItem& item);
    // Один вызов — один fixed-step; fetchResults завершается до записи узлов.
    bool Update(double fixedInterval);
};
