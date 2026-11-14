#include "UtilityAIComponent.h"
#include "core.h"
#include <sol/sol.hpp>
#include <cmath>
#include <iostream>
#include <tuple>

struct UtilityAIComponent::Impl {
    struct Action {
        sol::table self;
        sol::protected_function enter, execute, exit, available;
        bool failed = false;
    };
    std::vector<Action> actions;
    sol::table context;
    ~Impl() {
        // Сохранённый контекст больше не выдаёт владельца после Shutdown.
        if (context.valid()) {
            context.raw_set("object", sol::lua_nil);
            context.raw_set("ai", sol::lua_nil);
            context.raw_set("is_valid", false);
        }
    }
};

UtilityAIComponent::~UtilityAIComponent() { delete impl; }

void UtilityAIComponent::Fail(size_t action, const std::string& error)
{
    impl->actions[action].failed = true;
    lastError = table->GetActions()[action].name + ": " + error;
    std::cout << "[UtilityAI] " << lastError << std::endl;
}

void UtilityAIComponent::Stop(const char* reason)
{
    if (!impl || current < 0) return;
    const int previous = current;
    current = -1;
    // Активность снимается до вызова пользовательского кода.
    auto& action = impl->actions[previous];
    // Даже после ошибки выполнения пробуем завершить уже начатое действие один раз.
    if (action.exit.valid()) {
        auto result = action.exit(action.self, impl->context, reason);
        if (!result.valid()) { sol::error error = result; Fail(previous, error.what()); }
    }
    elapsed = 0;
}

void UtilityAIComponent::Shutdown(Game&)
{
    Stop("shutdown");
    delete impl;
    impl = nullptr;
    table = nullptr;
    values.clear(); snapshot.clear();
}

void UtilityAIComponent::Init(Game& game)
{
    Shutdown(game);
    lastError.clear(); decisionTime = 0;
    // Пустой путь оставляет добавленный из инспектора компонент без назначенной таблицы.
    if (tablePath.empty()) return;
    auto& resources = game.getResourceManager();
    if (auto path = resources.ResourcePath(tablePath); !path.empty()) tablePath = std::move(path);
    table = resources.load<UtilityAITable>(tablePath, &resources);
    if (!table) { lastError = resources.GetLastError(); std::cout << "[UtilityAI] " << lastError << std::endl; return; }
    for (const auto& parameter : table->GetParameters()) values.push_back(parameter.initialValue);
    if (game.isEditorMode()) return;
    lua_State* state = game.getScriptServer().getState();
    if (!state) { lastError = "Script server is not initialized"; return; }
    impl = new (std::nothrow) Impl();
    if (!impl) { lastError = "Cannot allocate UtilityAI runtime"; return; }
    sol::state_view lua(state);
    impl->context = lua.create_table();
    impl->context["object"] = getOwner();
    impl->context["ai"] = this;
    impl->context["is_valid"] = true;
    impl->actions.resize(table->GetActions().size());
    for (size_t i = 0; i < impl->actions.size(); ++i) {
        const auto path = resources.ResolvePath(table->GetActions()[i].scriptPath);
        if (path.empty()) { Fail(i, "Invalid script path"); continue; }
        // Каждый экземпляр получает собственный модуль и self.
        auto loaded = lua.safe_script_file(path, sol::script_pass_on_error);
        if (!loaded.valid()) { sol::error error = loaded; Fail(i, error.what()); continue; }
        if (loaded.return_count() == 0 || loaded.get_type() != sol::type::table) { Fail(i, "Action must return a table"); continue; }
        sol::table module = loaded.get<sol::table>();
        auto& action = impl->actions[i];
        action.self = lua.create_table();
        sol::table meta = lua.create_table();
        meta[sol::meta_function::index] = module;
        action.self[sol::metatable_key] = meta;
        const char* names[] = { "action_entered", "action_executing", "action_exited", "action_available" };
        sol::protected_function* functions[] = { &action.enter, &action.execute, &action.exit, &action.available };
        for (int f = 0; f < 4; ++f) {
            sol::object function = module.raw_get<sol::object>(names[f]);
            if (f == 3 && function.get_type() == sol::type::nil) continue;
            if (function.get_type() != sol::type::function) { Fail(i, std::string("Missing function: ") + names[f]); break; }
            *functions[f] = function.as<sol::protected_function>();
        }
    }
}

void UtilityAIComponent::SetTablePath(Game& game, std::string path)
{
    tablePath = std::move(path);
    Init(game);
}

int UtilityAIComponent::Find(const std::string& name, UtilityParameterType type, std::string& error) const
{
    error.clear();
    if (!table) { error = "UtilityAI is not initialized"; return -1; }
    const auto& parameters = table->GetParameters();
    for (size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].name != name) continue;
        if (parameters[i].type != type) { error = "Parameter type mismatch: " + name; return -1; }
        return static_cast<int>(i);
    }
    error = "Unknown parameter: " + name;
    return -1;
}

bool UtilityAIComponent::Get(const std::string& name, UtilityParameterType type, double& value, std::string& error) const
{
    const int index = Find(name, type, error);
    if (index < 0) return false;
    value = evaluating ? snapshot[index] : values[index];
    return true;
}

bool UtilityAIComponent::Set(const std::string& name, UtilityParameterType type, double value, std::string& error)
{
    const int index = Find(name, type, error);
    if (index < 0) return false;
    // Проверки доступности видят только снимок и не могут менять параметры.
    if (evaluating) { error = "Cannot modify parameters during evaluation"; return false; }
    if (!std::isfinite(value) || (type == UtilityParameterType::Int &&
        (std::trunc(value) != value || value < INT32_MIN || value > INT32_MAX))) {
        error = "Parameter value is outside its numeric domain"; return false;
    }
    values[index] = value;
    return true;
}

std::string UtilityAIComponent::GetCurrentAction() const
{
    return table && current >= 0 ? table->GetActions()[current].name : std::string{};
}

void UtilityAIComponent::Update(Game& game, double dt)
{
    if (game.isEditorMode() || !impl || !table) return;
    if (!enabled) { Stop("disabled"); decisionTime = 0; return; }
    if (!std::isfinite(dt) || dt < 0) return;
    elapsed += dt; decisionTime -= dt;
    if (decisionTime <= 0) {
        decisionTime = table->GetInterval();
        snapshot = values;
        evaluating = true;
        std::vector<double> scores(impl->actions.size(), -1);
        for (size_t i = 0; i < scores.size(); ++i) {
            auto& action = impl->actions[i];
            if (action.failed) continue;
            if (action.available.valid()) {
                auto result = action.available(action.self, impl->context);
                if (!result.valid()) { sol::error error = result; Fail(i, error.what()); continue; }
                if (result.return_count() == 0 || result.get_type() != sol::type::boolean) {
                    Fail(i, "action_available must return boolean"); continue;
                }
                if (!result.get<bool>()) continue;
            }
            double total = 0;
            size_t count = 0;
            const auto& curves = table->GetActions()[i].curves;
            for (size_t p = 0; p < curves.size(); ++p) {
                if (!curves[p]) continue;
                total += curves[p]->Evaluate(snapshot[p]); ++count;
            }
            scores[i] = total / static_cast<double>(count);
        }
        evaluating = false;
        // Удаление из Lua отложено, но новое действие уже нельзя запускать.
        if (!enabled || isPendingDestroy() || getOwner()->isPendingDestroy()) { Stop("disabled"); return; }
        if (current >= 0 && scores[current] < 0) Stop("unavailable");
        int best = current;
        for (size_t i = 0; i < scores.size(); ++i)
            if (scores[i] >= 0 && (best < 0 || scores[i] > scores[best])) best = static_cast<int>(i);
        if (best >= 0 && best != current && (current < 0 ||
            (elapsed >= table->GetMinimumDuration() && scores[best] > scores[current] + table->GetSwitchThreshold()))) {
            Stop("interrupted");
            if (!enabled || isPendingDestroy() || getOwner()->isPendingDestroy()) return;
            auto& action = impl->actions[best];
            auto result = action.enter(action.self, impl->context);
            if (!result.valid()) { sol::error error = result; Fail(best, error.what()); return; }
            current = best; elapsed = 0;
        }
    }
    if (current < 0) return;
    if (!enabled || isPendingDestroy() || getOwner()->isPendingDestroy()) { Stop("disabled"); return; }
    auto& action = impl->actions[current];
    auto result = action.execute(action.self, impl->context, dt);
    if (!result.valid()) { sol::error error = result; Fail(current, error.what()); Stop("error"); decisionTime = 0; return; }
    if (result.return_count() == 0 || result.get_type() != sol::type::string) {
        Fail(current, "action_executing must return running/success/failure"); Stop("error"); decisionTime = 0; return;
    }
    const std::string status = result.get<std::string>();
    if (status == "success" || status == "failure") { Stop(status.c_str()); decisionTime = 0; }
    else if (status != "running") { Fail(current, "Unknown action status: " + status); Stop("error"); decisionTime = 0; }
    else if (!enabled || isPendingDestroy() || getOwner()->isPendingDestroy()) Stop("disabled");
}

rapidjson::Value UtilityAIComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    const auto tableReference = ResourcePath(tablePath);
    if (!tablePath.empty() && tableReference.empty()) { error = "Resource path must be inside res://: " + tablePath; return {}; }

    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    json.AddMember("table_path", Serialization::String(tableReference, allocator), allocator);
    json.AddMember("enabled", enabled, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* UtilityAIComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    std::string path;
    bool active = true;
    if (!Deserialization::String(json, "table_path", path, error) || !Deserialization::Bool(json, "enabled", active, error)) return nullptr;
    auto* component = new (std::nothrow) UtilityAIComponent(std::move(path));
    if (!component) { error = "Cannot allocate UtilityAIComponent"; return nullptr; }
    component->enabled = active;
    return component;
}

namespace {
template<UtilityParameterType Type, bool Write>
std::tuple<sol::object, std::string> ParameterAccess(sol::this_state state, sol::variadic_args args)
{
    sol::state_view lua(state);
    auto nil = sol::make_object(lua, sol::lua_nil);
    if (args.size() != (Write ? 3 : 2) || !args[0].is<UtilityAIComponent*>() || !args[1].is<std::string>())
        return { nil, "Expected ai:get/set_float/int(name[, value])" };
    auto* ai = args[0].as<UtilityAIComponent*>();
    if (!ai || ai->isPendingDestroy()) return { nil, "UtilityAI is unavailable" };
    double value = 0;
    std::string error;
    const auto name = args[1].as<std::string>();
    if constexpr (Write) {
        if (args[2].get_type() != sol::type::number) return { nil, "Expected numeric value" };
        value = args[2].as<double>();
        const bool success = ai->Set(name, Type, value, error);
        return { sol::make_object(lua, success), error };
    } else {
        if (!ai->Get(name, Type, value, error)) return { nil, error };
        if constexpr (Type == UtilityParameterType::Int) return { sol::make_object(lua, static_cast<int>(value)), error };
        return { sol::make_object(lua, value), error };
    }
}
}

void BindUtilityAI(lua_State* state)
{
    sol::state_view lua(state);
    lua.new_usertype<UtilityAIComponent>("UtilityAI", sol::no_constructor,
        "get_float", &ParameterAccess<UtilityParameterType::Float, false>,
        "get_int", &ParameterAccess<UtilityParameterType::Int, false>,
        "set_float", &ParameterAccess<UtilityParameterType::Float, true>,
        "set_int", &ParameterAccess<UtilityParameterType::Int, true>,
        "current_action", sol::property(&UtilityAIComponent::GetCurrentAction),
        "last_error", sol::property(&UtilityAIComponent::GetLastError),
        "enabled", sol::property(&UtilityAIComponent::IsEnabled, &UtilityAIComponent::SetEnabled),
        "get_parameters", [](sol::this_state state, const UtilityAIComponent& ai) {
            sol::state_view lua(state);
            sol::table list = lua.create_table();
            if (!ai.GetTable()) return list;
            int index = 1;
            for (const auto& parameter : ai.GetTable()->GetParameters()) {
                sol::table entry = lua.create_table();
                entry["name"] = parameter.name;
                entry["type"] = parameter.type == UtilityParameterType::Int ? "int" : "float";
                double value = 0;
                std::string error;
                ai.Get(parameter.name, parameter.type, value, error);
                if (parameter.type == UtilityParameterType::Int) entry["value"] = static_cast<int>(value);
                else entry["value"] = value;
                list[index++] = entry;
            }
            return list;
        });
}
