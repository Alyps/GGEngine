#pragma once
// Общая точка подключения компонентов для существующего кода.
#include "Component.h"
#include "TransformableComponent.h"
#include "RenderComponent.h"
#include "MeshComponent.h"
#include "EditorCamera.h"
#include "CameraMovementComponent.h"
#include "LightComponent.h"
#include "LuaScriptComponent.h"
#include "PhysicsComponents.h"
#include "NavMeshVolumeComponent.h"
#include "NavAgentComponent.h"
#include "Delegates.h"

class ScriptServer;

// Объект владеет таймером напрямую: отсчёт не использует TransformTree.
class TimerComponent : public Component {
    float waitTime = 1.0f;
    bool repeating = false;
    bool autostart = false;
    bool running = false;
    bool runtimeConfigured = false;
    double timeLeft = 0.0;
    MulticastDelegate<> timeout;
    // Сервер заимствован у Game; Lua-мост освобождается раньше состояния Lua.
    ScriptServer* scriptServer = nullptr;
    DelegateHandle luaTimeoutHandle;
    void releaseLuaEvent();
public:
    ~TimerComponent() override;
    const char* GetSerializationType() const override { return "TimerComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
    bool setWaitTime(float seconds);
    float getWaitTime() const { return waitTime; }
    void setRepeating(bool value) { repeating = value; }
    bool isRepeating() const { return repeating; }
    void setAutostart(bool value) { autostart = value; }
    bool isAutostart() const { return autostart; }
    bool isRunning() const { return running; }
    double getTimeLeft() const { return timeLeft; }
    void start();
    void stop();
    MulticastDelegate<>& getTimeout() { return timeout; }
    // Кладёт Lua-представление события; скрипты могут подписываться, но не вызывать его.
    void PushTimeout(lua_State* state);
    void Init(Game& game) override;
    void Shutdown(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
