#pragma once
#include <string>
#include "TransformableComponent.h"

class GameObject;

// Запускает Lua-скрипт: файл возвращает таблицу с on_init(self) и on_update(self, dt).
// У каждого компонента своя таблица self; общая таблица модуля доступна через __index.
// В self доступны object (GameObject) и transform (TransformNode объекта).
class LuaScriptComponent : public TransformableComponent {
public:
    const char* GetSerializationType() const override { return "LuaScriptComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const override;
    // При успехе вызывающий код принимает владение компонентом до его привязки.
    static Component* CreateFromJson(const rapidjson::Value& json, std::string& error);
private:
    struct Impl;
    // Путь UTF-8, относительно рабочей папки процесса.
    std::string scriptPath;
    // Владеющий указатель; ссылки внутри живут в состоянии Lua из ScriptServer.
    Impl* impl = nullptr;
    // После ошибки скрипт отключается, остальные компоненты продолжают работать.
    bool failed = false;
public:
    explicit LuaScriptComponent(std::string path);
    // Game (и его ScriptServer) должен жить дольше компонента.
    ~LuaScriptComponent() override;
    LuaScriptComponent(const LuaScriptComponent&) = delete;
    LuaScriptComponent& operator=(const LuaScriptComponent&) = delete;
    void Init(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
