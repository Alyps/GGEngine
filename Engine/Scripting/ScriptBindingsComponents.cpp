#include "ScriptBindings.h"
#include "core.h"
#include <string>
#include <tuple>
#include <vector>
#include <sol/sol.hpp>

namespace {

// Проверяем self и Vector3 до преобразования: ошибки типов sol2 в режиме без
// исключений нельзя выпускать через его noexcept-переходник в C++-сборку Lua.
template<bool (DynamicRigidBodyComponent::*Method)(const DXVector3&)>
bool ApplyPhysicsAction(sol::variadic_args args)
{
	if (args.size() != 2 || !args[0].is<DynamicRigidBodyComponent*>() || !args[1].is<DXVector3>()) return false;
	auto* body = args[0].as<DynamicRigidBodyComponent*>();
	return body && (body->*Method)(args[1].as<DXVector3>());
}

// Та же проверка self и Vector3 для команд других компонентов.
template<class Target, bool (Target::*Method)(const DXVector3&)>
bool ApplyVectorCommand(sol::variadic_args args)
{
	if (args.size() != 2 || !args[0].is<Target*>() || !args[1].is<DXVector3>()) return false;
	auto* target = args[0].as<Target*>();
	return target && (target->*Method)(args[1].as<DXVector3>());
}

// Настройка меняется через проверяющий setSettings; недопустимое значение игнорируется.
template<class Target, class Settings, float Settings::* Field>
auto SettingProperty()
{
	return sol::property(
		[](const Target& target) { return target.getSettings().*Field; },
		[](Target& target, float value) { Settings settings = target.getSettings(); settings.*Field = value; target.setSettings(settings); });
}

void NavAgentStop(sol::variadic_args args)
{
	if (args.size() == 1 && args[0].is<NavAgentComponent*>())
		if (auto* agent = args[0].as<NavAgentComponent*>()) agent->stop();
}

const char* NavAgentStateName(NavAgentState state)
{
	switch (state) {
	case NavAgentState::Moving: return "moving";
	case NavAgentState::Arrived: return "arrived";
	case NavAgentState::Failed: return "failed";
	default: return "idle";
	}
}

// Копия углов текущего пути с индексами от 1.
sol::table NavAgentPath(sol::this_state state, const NavAgentComponent& agent)
{
	sol::state_view lua(state);
	const auto& path = agent.getPath();
	sol::table result = lua.create_table(static_cast<int>(path.size()), 0);
	for (size_t i = 0; i < path.size(); ++i) result[i + 1] = path[i];
	return result;
}

// Навигация: аргументы тоже проверяются до преобразования. Неудачный запрос даёт nil.
sol::object NavigationFindPath(sol::this_state state, sol::variadic_args args)
{
	sol::state_view lua(state);
	if (args.size() != 3 || !args[0].is<NavigationServer*>() || !args[1].is<DXVector3>() || !args[2].is<DXVector3>())
		return sol::make_object(lua, sol::lua_nil);
	auto* navigation = args[0].as<NavigationServer*>();
	std::vector<DXVector3> path;
	if (!navigation || !navigation->FindPath(args[1].as<DXVector3>(), args[2].as<DXVector3>(), path))
		return sol::make_object(lua, sol::lua_nil);
	// Массив углов пути с индексами от 1; первый угол — проекция начала на навмеш.
	sol::table result = lua.create_table(static_cast<int>(path.size()), 0);
	for (size_t i = 0; i < path.size(); ++i) result[i + 1] = path[i];
	return result;
}

sol::object NavigationProjectPoint(sol::this_state state, sol::variadic_args args)
{
	sol::state_view lua(state);
	if (args.size() != 2 || !args[0].is<NavigationServer*>() || !args[1].is<DXVector3>())
		return sol::make_object(lua, sol::lua_nil);
	auto* navigation = args[0].as<NavigationServer*>();
	DXVector3 result;
	if (!navigation || !navigation->ProjectPoint(args[1].as<DXVector3>(), result)) return sol::make_object(lua, sol::lua_nil);
	return sol::make_object(lua, result);
}

// Возвращает (упёрся ли луч в край навмеша, точка остановки или nil при ошибке запроса).
std::tuple<bool, sol::object> NavigationRaycast(sol::this_state state, sol::variadic_args args)
{
	sol::state_view lua(state);
	if (args.size() != 3 || !args[0].is<NavigationServer*>() || !args[1].is<DXVector3>() || !args[2].is<DXVector3>())
		return { false, sol::make_object(lua, sol::lua_nil) };
	auto* navigation = args[0].as<NavigationServer*>();
	DXVector3 hit;
	if (!navigation) return { false, sol::make_object(lua, sol::lua_nil) };
	const bool blocked = navigation->Raycast(args[1].as<DXVector3>(), args[2].as<DXVector3>(), hit);
	// Свободный луч оставляет lastError пустым; иначе запрос не выполнен.
	if (!blocked && !navigation->getLastError().empty()) return { false, sol::make_object(lua, sol::lua_nil) };
	return { blocked, sol::make_object(lua, hit) };
}

sol::object NavigationRandomPoint(sol::this_state state, sol::variadic_args args)
{
	sol::state_view lua(state);
	if (args.size() != 3 || !args[0].is<NavigationServer*>() || !args[1].is<DXVector3>() || !args[2].is<float>())
		return sol::make_object(lua, sol::lua_nil);
	auto* navigation = args[0].as<NavigationServer*>();
	DXVector3 result;
	if (!navigation || !navigation->RandomPointInRadius(args[1].as<DXVector3>(), args[2].as<float>(), result))
		return sol::make_object(lua, sol::lua_nil);
	return sol::make_object(lua, result);
}

// Ищет компонент по имени среди компонентов объекта; неизвестное имя даёт nil.
sol::object FindComponent(sol::this_state state, GameObject& object, const std::string& name)
{
	sol::state_view lua(state);
	for (Component* component : object.getComponents()) {
		if (name == "Mesh") {
			if (auto* typed = dynamic_cast<MeshComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "GameCamera") {
			if (auto* typed = dynamic_cast<GameCamera*>(component)) return sol::make_object(lua, typed);
		} else if (name == "DirectionalLight") {
			if (auto* typed = dynamic_cast<DirectionalLightComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "PointLight") {
			if (auto* typed = dynamic_cast<PointLightComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "LuaScript") {
			if (auto* typed = dynamic_cast<LuaScriptComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "BoxCollider") {
			if (auto* typed = dynamic_cast<BoxColliderComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "DynamicRigidBody") {
			if (auto* typed = dynamic_cast<DynamicRigidBodyComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "StaticRigidBody") {
			if (auto* typed = dynamic_cast<StaticRigidBodyComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "KinematicRigidBody") {
			if (auto* typed = dynamic_cast<KinematicRigidBodyComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "CharacterMovement") {
			if (auto* typed = dynamic_cast<CharacterMovementComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "NavMeshVolume") {
			if (auto* typed = dynamic_cast<NavMeshVolumeComponent*>(component)) return sol::make_object(lua, typed);
		} else if (name == "NavAgent") {
			if (auto* typed = dynamic_cast<NavAgentComponent*>(component)) return sol::make_object(lua, typed);
#if defined(GG_EDITOR)
		} else if (name == "EditorCamera") {
			if (auto* typed = dynamic_cast<EditorCamera*>(component)) return sol::make_object(lua, typed);
		} else if (name == "CameraMovement") {
			if (auto* typed = dynamic_cast<CameraMovementComponent*>(component)) return sol::make_object(lua, typed);
#endif
		}
	}
	return sol::make_object(lua, sol::lua_nil);
}

}

void BindComponents(lua_State* L)
{
	sol::state_view lua(L);

	// Поддерживать ComponentType, FindComponent и Lua-типы синхронно при добавлении/переименовании компонентов.
	// Строковые константы доступны через ComponentType.PointLight; обычное присваивание запрещено.
	lua.new_enum<true>("ComponentType",
		"Mesh", "Mesh", "GameCamera", "GameCamera",
		"DirectionalLight", "DirectionalLight", "PointLight", "PointLight",
		"LuaScript", "LuaScript", "BoxCollider", "BoxCollider",
		"DynamicRigidBody", "DynamicRigidBody", "StaticRigidBody", "StaticRigidBody",
		"KinematicRigidBody", "KinematicRigidBody", "CharacterMovement", "CharacterMovement",
		"NavMeshVolume", "NavMeshVolume", "NavAgent", "NavAgent"
#if defined(GG_EDITOR)
		, "EditorCamera", "EditorCamera", "CameraMovement", "CameraMovement"
#endif
	);
	// new_enum возвращает state_view: саму таблицу констант получаем по имени.
	sol::table componentTypes = lua["ComponentType"];
	// Скрываем метатаблицу констант от обычных getmetatable/setmetatable.
	sol::table componentTypesMeta = componentTypes[sol::metatable_key];
	componentTypesMeta[sol::meta_function::metatable] = false;

	// Эти типы пока доступны как ссылки без полей; конструкторы и владение остаются в C++.
	lua.new_usertype<LuaScriptComponent>("LuaScript", sol::no_constructor);
#if defined(GG_EDITOR)
	lua.new_usertype<EditorCamera>("EditorCamera", sol::no_constructor);
	lua.new_usertype<CameraMovementComponent>("CameraMovement", sol::no_constructor);
#endif

	// Компоненты и объекты передаются указателями; владение остаётся у C++.
	lua.new_usertype<GameObject>("GameObject",
		sol::no_constructor,
		"transform", sol::property([](GameObject& o) { return &o.getTransform(); }),
		"get_component", &FindComponent);

	// Mesh пока не имеет настраиваемых полей: тип нужен, чтобы найти и проверить компонент.
	lua.new_usertype<MeshComponent>("Mesh", sol::no_constructor);

	// Свойства камеры пока не доступны скриптам: тип нужен, чтобы найти и проверить компонент.
	lua.new_usertype<GameCamera>("GameCamera", sol::no_constructor);

	// Lua заимствует компоненты объекта, не получает Item или ID тела Jolt.
	// Методы принимают мировые Vector3 и возвращают bool принятия команды.
	lua.new_usertype<DynamicRigidBodyComponent>("DynamicRigidBody", sol::no_constructor,
		"add_force", &ApplyPhysicsAction<&DynamicRigidBodyComponent::addForce>,
		"add_impulse", &ApplyPhysicsAction<&DynamicRigidBodyComponent::addImpulse>,
		"add_torque", &ApplyPhysicsAction<&DynamicRigidBodyComponent::addTorque>,
		"add_angular_impulse", &ApplyPhysicsAction<&DynamicRigidBodyComponent::addAngularImpulse>,
		// Команды суммируются до ближайшего fixed-step; сила и момент действуют один шаг.
		// Состояние читается после завершённого шага, векторы возвращаются копиями.
		"linear_velocity", sol::property([](const DynamicRigidBodyComponent& c) { return DXVector3(c.getPhysicsItem().linearVelocity); }),
		"angular_velocity", sol::property([](const DynamicRigidBodyComponent& c) { return DXVector3(c.getPhysicsItem().angularVelocity); }),
		"mass", sol::property([](const DynamicRigidBodyComponent& c) { return c.getPhysicsItem().mass; }),
		"has_actor", sol::property([](const DynamicRigidBodyComponent& c) { return c.getPhysicsItem().hasActor(); }),
		"is_sleeping", sol::property([](const DynamicRigidBodyComponent& c) { return c.getPhysicsItem().isSleeping(); }),
		"enabled", sol::property([](const DynamicRigidBodyComponent& c) { return c.getPhysicsItem().enabled; },
			[](DynamicRigidBodyComponent& c, bool enabled) { c.getPhysicsItem().enabled = enabled; }));

	// Статическое тело не предоставляет методы сил и импульсов.
	lua.new_usertype<StaticRigidBodyComponent>("StaticRigidBody", sol::no_constructor,
		"has_actor", sol::property([](const StaticRigidBodyComponent& c) { return c.getPhysicsItem().hasActor(); }),
		"enabled", sol::property([](const StaticRigidBodyComponent& c) { return c.getPhysicsItem().enabled; },
			[](StaticRigidBodyComponent& c, bool enabled) { c.getPhysicsItem().enabled = enabled; }));

	// Скорости действуют до следующей записи; перенос узла тоже двигает тело.
	lua.new_usertype<KinematicRigidBodyComponent>("KinematicRigidBody", sol::no_constructor,
		"linear_velocity", sol::property([](const KinematicRigidBodyComponent& c) { return DXVector3(c.getPhysicsItem().linearVelocity); },
			[](KinematicRigidBodyComponent& c, const DXVector3& velocity) { c.setLinearVelocity(velocity); }),
		"angular_velocity", sol::property([](const KinematicRigidBodyComponent& c) { return DXVector3(c.getPhysicsItem().angularVelocity); },
			[](KinematicRigidBodyComponent& c, const DXVector3& velocity) { c.setAngularVelocity(velocity); }),
		"has_actor", sol::property([](const KinematicRigidBodyComponent& c) { return c.getPhysicsItem().hasActor(); }),
		"is_sleeping", sol::property([](const KinematicRigidBodyComponent& c) { return c.getPhysicsItem().isSleeping(); }),
		"enabled", sol::property([](const KinematicRigidBodyComponent& c) { return c.getPhysicsItem().enabled; },
			[](KinematicRigidBodyComponent& c, bool enabled) { c.getPhysicsItem().enabled = enabled; }));

	// move задаёт скорость по XZ до следующего вызова; состояние читается после физического шага.
	lua.new_usertype<CharacterMovementComponent>("CharacterMovement", sol::no_constructor,
		"move", &ApplyVectorCommand<CharacterMovementComponent, &CharacterMovementComponent::move>,
		"velocity", sol::property([](const CharacterMovementComponent& c) { return DXVector3(c.getCharacterItem().velocity); }),
		"is_grounded", sol::property([](const CharacterMovementComponent& c) { return c.getCharacterItem().grounded; }),
		"radius", sol::property([](const CharacterMovementComponent& c) { return c.getSettings().radius; }),
		"gravity_scale", SettingProperty<CharacterMovementComponent, CharacterSettings, &CharacterSettings::gravityScale>(),
		"step_height", SettingProperty<CharacterMovementComponent, CharacterSettings, &CharacterSettings::stepHeight>(),
		"enabled", sol::property([](const CharacterMovementComponent& c) { return c.getCharacterItem().enabled; },
			[](CharacterMovementComponent& c, bool enabled) { c.getCharacterItem().enabled = enabled; }));

	// Размер читается копией; включение коллайдера учитывается системой на fixed-step.
	lua.new_usertype<BoxColliderComponent>("BoxCollider", sol::no_constructor,
		"size", sol::property([](const BoxColliderComponent& c) { return DXVector3(c.getColliderItem().size); }),
		"enabled", sol::property([](const BoxColliderComponent& c) { return c.getColliderItem().enabled; },
			[](BoxColliderComponent& c, bool enabled) { c.getColliderItem().enabled = enabled; }),
		// Флаг читается только генератором навмеша в редакторе.
		"affects_navmesh", sol::property([](const BoxColliderComponent& c) { return c.getColliderItem().affectsNavmesh; },
			[](BoxColliderComponent& c, bool affects) { c.getColliderItem().affectsNavmesh = affects; }));
#if defined(GG_GAME)
	// Скрипты получают заимствованный GameInput через self.input; создавать его нельзя.
	lua.new_usertype<GameInput>("GameInput", sol::no_constructor,
		"is_key_down", &GameInput::IsKeyDown,
		"was_key_pressed", &GameInput::WasKeyPressed,
		"was_key_released", &GameInput::WasKeyReleased,
		"active", sol::property(&GameInput::IsActive),
		"mouse_x", sol::property([](const GameInput& input) { return input.GetMousePosition().x; }),
		"mouse_y", sol::property([](const GameInput& input) { return input.GetMousePosition().y; }),
		"mouse_delta_x", sol::property([](const GameInput& input) { return input.GetMouseDelta().x; }),
		"mouse_delta_y", sol::property([](const GameInput& input) { return input.GetMouseDelta().y; }),
		"wheel_delta", sol::property(&GameInput::GetMouseWheelDelta));

	// Имена совпадают с C++ Keys; буквенные и цифровые коды идут последовательно.
	auto keys = lua.create_named_table("Keys");
	for (int code = 'A'; code <= 'Z'; ++code) keys[std::string(1, static_cast<char>(code))] = static_cast<Keys>(code);
	for (int code = '0'; code <= '9'; ++code) keys["D" + std::string(1, static_cast<char>(code))] = static_cast<Keys>(code);
	keys["LeftButton"] = Keys::LeftButton; keys["RightButton"] = Keys::RightButton;
	keys["MiddleButton"] = Keys::MiddleButton; keys["Escape"] = Keys::Escape;
	keys["Space"] = Keys::Space; keys["LeftShift"] = Keys::LeftShift;
	keys["RightShift"] = Keys::RightShift;
	keys["Up"] = Keys::Up; keys["Down"] = Keys::Down;
	keys["Left"] = Keys::Left; keys["Right"] = Keys::Right;
#endif

	// Скрипты получают заимствованную систему навигации через self.navigation; создавать её нельзя.
	// Навмеш статический: запросы работают после загрузки данных объёмами уровня.
	lua.new_usertype<NavigationServer>("Navigation", sol::no_constructor,
		"has_navmesh", sol::property([](const NavigationServer& n) { return n.hasNavMesh(); }),
		"last_error", sol::property([](const NavigationServer& n) { return n.getLastError(); }),
		"find_path", &NavigationFindPath,
		"project_point", &NavigationProjectPoint,
		"raycast", &NavigationRaycast,
		"random_point", &NavigationRandomPoint);

	lua.new_usertype<NavMeshVolumeComponent>("NavMeshVolume", sol::no_constructor);

	// Агент независим от других агентов; движение выполняет CharacterMovement того же объекта.
	lua.new_usertype<NavAgentComponent>("NavAgent", sol::no_constructor,
		"set_destination", &ApplyVectorCommand<NavAgentComponent, &NavAgentComponent::setDestination>,
		"warp", &ApplyVectorCommand<NavAgentComponent, &NavAgentComponent::warp>,
		"stop", &NavAgentStop,
		"speed", SettingProperty<NavAgentComponent, NavAgentSettings, &NavAgentSettings::speed>(),
		"acceleration", SettingProperty<NavAgentComponent, NavAgentSettings, &NavAgentSettings::acceleration>(),
		"angular_speed", SettingProperty<NavAgentComponent, NavAgentSettings, &NavAgentSettings::angularSpeed>(),
		"stopping_distance", SettingProperty<NavAgentComponent, NavAgentSettings, &NavAgentSettings::stoppingDistance>(),
		"update_rotation", sol::property([](const NavAgentComponent& a) { return a.getSettings().updateRotation; },
			[](NavAgentComponent& a, bool value) { NavAgentSettings settings = a.getSettings(); settings.updateRotation = value; a.setSettings(settings); }),
		"state", sol::property([](const NavAgentComponent& a) { return NavAgentStateName(a.getState()); }),
		"destination", sol::property([](const NavAgentComponent& a) { return DXVector3(a.getDestination()); }),
		"is_partial", sol::property([](const NavAgentComponent& a) { return a.isPartial(); }),
		"remaining_distance", sol::property([](const NavAgentComponent& a) { return a.getRemainingDistance(); }),
		"velocity", sol::property([](const NavAgentComponent& a) { return a.getVelocity(); }),
		"path", sol::property(&NavAgentPath),
		"last_error", sol::property([](const NavAgentComponent& a) { return a.getLastError(); }));

	// Общие поля света; цвет передаётся как Color, альфа игнорируется.
	lua.new_usertype<LightComponent>("Light",
		sol::no_constructor,
		"color", sol::property([](const LightComponent& l) { return DXColor(l.color.x, l.color.y, l.color.z, 1.0f); },
			[](LightComponent& l, const DXColor& c) { l.color = DirectX::XMFLOAT3(c.x, c.y, c.z); }),
		"intensity", sol::property([](const LightComponent& l) { return l.intensity; },
			[](LightComponent& l, float value) { l.intensity = value; }),
		"enabled", sol::property([](const LightComponent& l) { return l.enabled; },
			[](LightComponent& l, bool value) { l.enabled = value; }));

	lua.new_usertype<DirectionalLightComponent>("DirectionalLight",
		sol::no_constructor,
		sol::base_classes, sol::bases<LightComponent>());

	lua.new_usertype<PointLightComponent>("PointLight",
		sol::no_constructor,
		"range", sol::property([](const PointLightComponent& l) { return l.range; },
			[](PointLightComponent& l, float value) { l.range = value; }),
		sol::base_classes, sol::bases<LightComponent>());
}
