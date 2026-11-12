#include "LuaScriptComponent.h"
#include "ScriptBindings.h"
#include "Delegates.h"
#include "core.h"
#include <iostream>
#include <new>
#include <vector>
#include <sol/sol.hpp>

namespace {
constexpr const char* delegateLifecycleKey = "GGEngine.DelegateLifecycle";

// Снимок неизменяем: Broadcast не сталкивается с перестройкой своего массива.
void DispatchDelegates(sol::table snapshot, sol::variadic_args args)
{
    std::vector<sol::protected_function> callbacks;
    callbacks.reserve(snapshot.size());
    for (size_t i = 1; i <= snapshot.size(); ++i)
        callbacks.push_back(snapshot.get<sol::protected_function>(i));

    // Указатели заимствованы из локального массива, живущего до конца Broadcast.
    MulticastDelegate<const sol::variadic_args&> multicast;
    for (auto& callback : callbacks) {
        multicast.AddLambda([function = &callback](const sol::variadic_args& values) {
            sol::protected_function_result result = (*function)(values);
            if (!result.valid()) {
                sol::error error = result;
                std::cout << "[Lua] Delegate: " << error.what() << std::endl;
            }
        });
    }
    multicast.Broadcast(args);
}

// Реестр хранит только Lua-таблицы: циклы callback -> self доступны сборщику Lua.
void SetDelegateOwner(const sol::table& instance, bool active)
{
    sol::state_view lua(instance.lua_state());
    sol::table lifecycle = lua.registry()[delegateLifecycleKey];
    sol::protected_function operation = lifecycle[active ? "add" : "remove"];
    sol::protected_function_result result = operation(instance);
    if (!result.valid()) {
        sol::error error = result;
        std::cout << "[Lua] Delegate lifecycle: " << error.what() << std::endl;
    }
}
}

bool BindDelegates(lua_State* L)
{
    sol::state_view lua(L);
    // Замыкания скрывают служебные таблицы; C++ не удерживает Lua-ссылки между вызовами.
    auto loaded = lua.safe_script(R"lua(
return function(dispatch)
    local owners = {}
    local assert, type, next = assert, type, next
    local remove = table.remove
    local api = {}

    function api.new(owner)
        local lifetime = owners[owner]
        assert(lifetime, "Delegate.new expects a live script self")
        local state = { listeners = {}, lifetime = lifetime }
        lifetime.events[state] = true
        local event = {}

        function event:connect(subscriber, callback)
            local target = owners[subscriber]
            assert(state.lifetime, "Delegate owner is no longer active")
            assert(target, "connect expects a live subscriber self")
            assert(type(callback) == "function", "connect expects a function")
            local entry = { owner = subscriber, callback = callback, target = target }
            state.listeners[#state.listeners + 1] = entry
            target.subscriptions[entry] = true

            -- disconnect освобождает захваченные self и callback даже при сохранённом handle.
            function entry.disconnect()
                if not entry.target then return false end
                entry.target.subscriptions[entry] = nil
                entry.target, entry.owner, entry.callback = nil, nil, nil
                for i = 1, #state.listeners do
                    if state.listeners[i] == entry then
                        remove(state.listeners, i)
                        break
                    end
                end
                return true
            end

            -- Проверка перед каждым вызовом учитывает отключение внутри другого обработчика.
            function entry.invoke(...)
                if state.lifetime and entry.target then
                    entry.callback(entry.owner, ...)
                end
            end
            return { disconnect = function() return entry.disconnect() end }
        end

        function event:emit(...)
            if not state.lifetime then return false end
            local snapshot = {}
            for i = 1, #state.listeners do
                snapshot[i] = state.listeners[i].invoke
            end
            -- Аргументы передаются без упаковки в массив: сохраняются nil и их количество.
            dispatch(snapshot, ...)
            return true
        end
        return event
    end

    local lifecycle = {}
    function lifecycle.add(owner)
        owners[owner] = { events = {}, subscriptions = {} }
    end

    -- C++-источник получает отдельное время жизни и публичный вид без emit.
    function lifecycle.native_event(owner, name)
        if not owners[owner] then lifecycle.add(owner) end
        local lifetime = owners[owner]
        if not lifetime.native then lifetime.native = {} end
        local record = lifetime.native[name]
        if not record then
            local event = api.new(owner)
            record = { event = event, view = { connect = event.connect } }
            lifetime.native[name] = record
        end
        return record.view
    end

    function lifecycle.native_emit(owner, name, ...)
        local lifetime = owners[owner]
        local record = lifetime and lifetime.native and lifetime.native[name]
        if record then record.event:emit(...) end
    end

    function lifecycle.remove(owner)
        local lifetime = owners[owner]
        if not lifetime then return end
        owners[owner] = nil
        -- Сначала закрываем исходящие события, затем оставшиеся входящие подписки.
        local state = next(lifetime.events)
        while state do
            state.lifetime = nil
            while #state.listeners > 0 do
                state.listeners[#state.listeners].disconnect()
            end
            lifetime.events[state] = nil
            state = next(lifetime.events)
        end
        local entry = next(lifetime.subscriptions)
        while entry do
            entry.disconnect()
            entry = next(lifetime.subscriptions)
        end
    end
    return api, lifecycle
end
)lua", sol::script_pass_on_error);
    if (!loaded.valid()) {
        sol::error error = loaded;
        std::cout << "[Lua] Delegate setup: " << error.what() << std::endl;
        return false;
    }

    sol::protected_function setup = loaded.get<sol::protected_function>();
    auto result = setup(&DispatchDelegates);
    if (!result.valid()) {
        sol::error error = result;
        std::cout << "[Lua] Delegate setup: " << error.what() << std::endl;
        return false;
    }
    lua["Delegate"] = result.get<sol::table>(0);
    lua.registry()[delegateLifecycleKey] = result.get<sol::table>(1);
    return true;
}

void ScriptServer::PushNativeEvent(const void* owner, const char* name)
{
    lua_State* L = getState();
    if (!L) return;
    sol::state_view lua(L);
    sol::table lifecycle = lua.registry()[delegateLifecycleKey];
    sol::protected_function create = lifecycle["native_event"];
    sol::object event = sol::make_object(lua, sol::lua_nil);
    {
        auto result = create(sol::lightuserdata_value(const_cast<void*>(owner)), name);
        if (result.valid()) event = result.get<sol::object>();
        else {
            sol::error error = result;
            std::cout << "[Lua] Native delegate: " << error.what() << std::endl;
        }
    }
    // Результат уже убран со стека; возвращаем сохранённое представление события.
    event.push();
}

void ScriptServer::EmitNativeEvent(const void* owner, const char* name)
{
    if (!getState()) return;
    sol::state_view lua(getState());
    sol::table lifecycle = lua.registry()[delegateLifecycleKey];
    sol::protected_function emit = lifecycle["native_emit"];
    auto result = emit(sol::lightuserdata_value(const_cast<void*>(owner)), name);
    if (!result.valid()) {
        sol::error error = result;
        std::cout << "[Lua] Native delegate: " << error.what() << std::endl;
    }
}

void ScriptServer::EmitNativeEvent(const void* owner, const char* name, GameObject* other)
{
    if (!getState() || !other || other->isPendingDestroy()) return;
    sol::state_view lua(getState());
    sol::table lifecycle = lua.registry()[delegateLifecycleKey];
    sol::protected_function emit = lifecycle["native_emit"];
    auto result = emit(sol::lightuserdata_value(const_cast<void*>(owner)), name, other);
    if (!result.valid()) {
        sol::error error = result;
        std::cout << "[Lua] Native delegate: " << error.what() << std::endl;
    }
}

void BoxTriggerComponent::PushEvent(lua_State* state, bool entered)
{
    Game* game = getOwner() ? getOwner()->getGame() : nullptr;
    if (!game || game->isEditorMode() || isPendingDestroy() || getOwner()->isPendingDestroy()
        || game->getScriptServer().getState() != state) {
        lua_pushnil(state);
        return;
    }
    // C++-замыкания заимствуют только компонент, Lua-функциями владеет реестр Lua.
    if (!scriptServer) {
        scriptServer = &game->getScriptServer();
        enteredHandle = triggerItem.bodyEntered.AddLambda([this](GameObject* other) {
            scriptServer->EmitNativeEvent(this, "body_entered", other);
        });
        exitedHandle = triggerItem.bodyExited.AddLambda([this](GameObject* other) {
            scriptServer->EmitNativeEvent(this, "body_exited", other);
        });
    }
    scriptServer->PushNativeEvent(this, entered ? "body_entered" : "body_exited");
}

void ScriptServer::RemoveNativeEvents(const void* owner)
{
    if (!getState()) return;
    sol::state_view lua(getState());
    sol::table lifecycle = lua.registry()[delegateLifecycleKey];
    sol::protected_function remove = lifecycle["remove"];
    auto result = remove(sol::lightuserdata_value(const_cast<void*>(owner)));
    if (!result.valid()) {
        sol::error error = result;
        std::cout << "[Lua] Native delegate cleanup: " << error.what() << std::endl;
    }
}

void TimerComponent::PushTimeout(lua_State* state)
{
    Game* game = getOwner() ? getOwner()->getGame() : nullptr;
    if (!game || game->isEditorMode() || isPendingDestroy() || getOwner()->isPendingDestroy()
        || game->getScriptServer().getState() != state) {
        lua_pushnil(state);
        return;
    }
    // Один C++-обработчик на источник; все Lua-подписчики принадлежат реестру Lua.
    if (!scriptServer) {
        scriptServer = &game->getScriptServer();
        luaTimeoutHandle = timeout.AddLambda([this]() { scriptServer->EmitNativeEvent(this, "timeout"); });
    }
    scriptServer->PushNativeEvent(this, "timeout");
}

struct LuaScriptComponent::Impl {
    sol::table instance;
    sol::protected_function onInit;
    sol::protected_function onUpdate;
    bool ready = false;

    ~Impl()
    {
        if (!instance.valid()) return;
        SetDelegateOwner(instance, false);
        // Сохранённая другим скриптом таблица больше не ссылается на владельца.
        instance.raw_set("is_valid", false);
        instance.raw_set("object", sol::lua_nil);
        instance.raw_set("transform", sol::lua_nil);
        instance.raw_set("level", sol::lua_nil);
        instance.raw_set("input", sol::lua_nil);
        // Убираем методы модуля, не вызывая пользовательские метаметоды.
        instance[sol::metatable_key] = sol::lua_nil;
    }
};

LuaScriptComponent::LuaScriptComponent(std::string path)
    : scriptPath(std::move(path)) {}

LuaScriptComponent::~LuaScriptComponent()
{
    delete impl;
}

void LuaScriptComponent::PushInstance(lua_State* state) const
{
    // Не выдаём частично инициализированный экземпляр или таблицу другого состояния.
    if (!impl || !impl->ready || failed || impl->instance.lua_state() != state) {
        lua_pushnil(state);
        return;
    }
    impl->instance.push(state);
}

void LuaScriptComponent::Init(Game& game)
{
    // Повторная инициализация начинает с чистого экземпляра.
    delete impl;
    impl = nullptr;
    failed = false;
    if (game.isEditorMode()) return;

    lua_State* L = game.getScriptServer().getState();
    if (!L) {
        std::cout << "[Lua] " << scriptPath << ": script server is not initialized" << std::endl;
        failed = true;
        return;
    }
    sol::state_view lua(L);

    // Выполняем файл; ошибки возвращаются результатом, а не прерывают движок.
    const std::string resolved = game.getResourceManager().ResolvePath(scriptPath);
    if (resolved.empty()) { failed = true; std::cout << "[Lua] Invalid script path\n"; return; }
    sol::protected_function_result loaded = lua.safe_script_file(resolved, sol::script_pass_on_error);
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
    SetDelegateOwner(impl->instance, true);
    impl->instance.raw_set("is_valid", true);
    sol::table meta = lua.create_table();
    meta[sol::meta_function::index] = module;
    impl->instance[sol::metatable_key] = meta;

    // Владелец берётся из Component; явно выбранную ветку сохраняем.
    if (GameObject* object = getOwner()) {
        impl->instance["object"] = object;
        // Уровень владеет объектом и живёт дольше экземпляра скрипта.
        impl->instance["level"] = object->getOwner();
        if (!transformNode) bind_to_node(&object->getTransform());
    }
    if (transformNode) impl->instance["transform"] = transformNode;
#if defined(GG_GAME)
    // Game владеет вводом и живёт дольше скриптовых компонентов уровня.
    impl->instance["input"] = &game.getGameInput();
#endif

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
    // Другие компоненты получают экземпляр только после успешного on_init.
    impl->ready = !failed;
    if (failed) SetDelegateOwner(impl->instance, false);
}

void LuaScriptComponent::Update(Game& game, double deltaTime)
{
    if (game.isEditorMode()) return;
    if (failed || !impl || !impl->onUpdate.valid()) return;

    sol::protected_function_result result = impl->onUpdate(impl->instance, deltaTime);
    if (!result.valid()) {
        // Отключаем только этот скрипт, чтобы ошибка не повторялась каждый кадр.
        sol::error error = result;
        std::cout << "[Lua] " << scriptPath << ": " << error.what() << std::endl;
        failed = true;
        SetDelegateOwner(impl->instance, false);
    }
}

rapidjson::Value LuaScriptComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    // Таблица self и состояние выполнения остаются только в runtime.
    json.AddMember("script_path", Serialization::String(scriptPath, allocator), allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* LuaScriptComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    error.clear();
    using namespace Deserialization;
    std::string path;
    if (!String(json, "script_path", path, error)) return nullptr;
    // До привязки созданным компонентом владеет вызывающий код.
    auto* result = new (std::nothrow) LuaScriptComponent(std::move(path));
    if (!result) { error = "Cannot allocate component"; return nullptr; }
    return result;
}
