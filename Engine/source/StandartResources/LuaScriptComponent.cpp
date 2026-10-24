#include "StandartResources/LuaScriptComponent.h"
#include "core.h"
#include <iostream>
#include <new>
#include <sol/sol.hpp>

struct LuaScriptComponent::Impl {
    sol::table instance;
    sol::protected_function onInit;
    sol::protected_function onUpdate;
};

LuaScriptComponent::LuaScriptComponent(std::string path)
    : scriptPath(std::move(path)) {}

LuaScriptComponent::~LuaScriptComponent()
{
    delete impl;
}

void LuaScriptComponent::Init(Game& game)
{
    // Повторная инициализация начинает с чистого экземпляра.
    delete impl;
    impl = nullptr;
    failed = false;

    lua_State* L = game.getScriptServer().getState();
    if (!L) {
        std::cout << "[Lua] " << scriptPath << ": script server is not initialized" << std::endl;
        failed = true;
        return;
    }
    sol::state_view lua(L);

    // Выполняем файл; ошибки возвращаются результатом, а не прерывают движок.
    sol::protected_function_result loaded = lua.safe_script_file(scriptPath, sol::script_pass_on_error);
    if (!loaded.valid()) {
        sol::error error = loaded;
        std::cout << "[Lua] " << scriptPath << ": " << error.what() << std::endl;
        failed = true;
        return;
    }
    sol::object returned = loaded.get<sol::object>();
    if (returned.get_type() != sol::type::table) {
        std::cout << "[Lua] " << scriptPath << ": script must return a table" << std::endl;
        failed = true;
        return;
    }
    sol::table module = returned.as<sol::table>();

    impl = new (std::nothrow) Impl();
    if (!impl) {
        failed = true;
        return;
    }

    // self получает методы модуля через __index, но хранит своё состояние отдельно.
    impl->instance = lua.create_table();
    sol::table meta = lua.create_table();
    meta[sol::meta_function::index] = module;
    impl->instance[sol::metatable_key] = meta;

    // Владелец берётся из Component; явно выбранную ветку сохраняем.
    if (GameObject* object = getOwner()) {
        impl->instance["object"] = object;
        if (!transformNode) bind_to_node(&object->getTransform());
    }
    if (transformNode) impl->instance["transform"] = transformNode;

    // Функции берём один раз: покадровый вызов не ищет их по имени.
    sol::object init = module["on_init"];
    if (init.get_type() == sol::type::function)
        impl->onInit = init.as<sol::protected_function>();
    sol::object update = module["on_update"];
    if (update.get_type() == sol::type::function)
        impl->onUpdate = update.as<sol::protected_function>();

    if (impl->onInit.valid()) {
        sol::protected_function_result result = impl->onInit(impl->instance);
        if (!result.valid()) {
            sol::error error = result;
            std::cout << "[Lua] " << scriptPath << ": " << error.what() << std::endl;
            failed = true;
        }
    }
}

void LuaScriptComponent::Update(Game& game, double deltaTime)
{
    if (failed || !impl || !impl->onUpdate.valid()) return;

    sol::protected_function_result result = impl->onUpdate(impl->instance, deltaTime);
    if (!result.valid()) {
        // Отключаем только этот скрипт, чтобы ошибка не повторялась каждый кадр.
        sol::error error = result;
        std::cout << "[Lua] " << scriptPath << ": " << error.what() << std::endl;
        failed = true;
    }
}
