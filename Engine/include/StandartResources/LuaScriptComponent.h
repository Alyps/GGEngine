#pragma once
#include <string>
#include "TransformableComponent.h"

class GameObject;

// Запускает Lua-скрипт: файл возвращает таблицу с on_init(self) и on_update(self, dt).
// У каждого компонента своя таблица self; общая таблица модуля доступна через __index.
// В self доступны object (GameObject) и transform (TransformNode объекта).
class LuaScriptComponent : public TransformableComponent {
private:
    struct Impl;
    // Путь UTF-8, относительно рабочей папки процесса.
    std::string scriptPath;
    // Невладеющая ссылка: объект должен жить дольше компонента.
    GameObject* ownerObject = nullptr;
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
    // Привязывает объект и его узел трансформации; вызывается до Init.
    void bind_to_object(GameObject* object);
    void Init(Game& game) override;
    void Update(Game& game, double deltaTime) override;
};
