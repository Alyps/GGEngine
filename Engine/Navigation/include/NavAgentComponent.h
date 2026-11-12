#pragma once
#include "TransformableComponent.h"
#include "NavigationItem.h"

class CharacterMovementComponent;

// Ведёт объект по навмешу, передавая скорость CharacterMovementComponent того же объекта.
// Каждый агент независим: обхода других агентов и логики толпы нет.
class NavAgentComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "NavAgentComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    NavAgentItem agentItem;
    // Game заимствован между Init и Shutdown и живёт дольше компонента.
    Game* activeGame = nullptr;
    CharacterMovementComponent* findCharacter() const;
    // Строит путь от текущей позиции к destination; false — агент перешёл в Failed.
    bool plan();
    bool fail(std::string reason);
    void halt(NavAgentState state);
public:
    NavAgentComponent() = default;
    const NavAgentSettings& getSettings() const { return agentItem.settings; }
    bool setSettings(const NavAgentSettings& settings, std::string* error = nullptr);
    // Мировая точка назначения; false — путь не построен, причина в getLastError.
    bool setDestination(const DXVector3& target);
    void stop();
    // Мгновенный перенос на ближайшую точку навмеша с отменой текущего пути.
    bool warp(const DXVector3& point);
    bool hasCharacter() const { return findCharacter() != nullptr; }
    NavAgentState getState() const { return agentItem.state; }
    const DXVector3& getDestination() const { return agentItem.destination; }
    const std::vector<DXVector3>& getPath() const { return agentItem.path; }
    bool isPartial() const { return agentItem.partial; }
    float getRemainingDistance() const { return agentItem.remainingDistance; }
    const std::string& getLastError() const { return agentItem.lastError; }
    DXVector3 getVelocity() const;
    // Применяет проверенное описание к существующему компоненту (Undo редактора).
    bool ApplyDescription(const rapidjson::Value& json, std::string& error);
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
