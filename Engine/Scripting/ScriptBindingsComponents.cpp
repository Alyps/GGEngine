#include "ScriptBindings.h"
#include "core.h"
#include <string>
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
		"DynamicRigidBody", "DynamicRigidBody", "StaticRigidBody", "StaticRigidBody"
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

	// Размер читается копией; включение коллайдера учитывается системой на fixed-step.
	lua.new_usertype<BoxColliderComponent>("BoxCollider", sol::no_constructor,
		"size", sol::property([](const BoxColliderComponent& c) { return DXVector3(c.getColliderItem().size); }),
		"enabled", sol::property([](const BoxColliderComponent& c) { return c.getColliderItem().enabled; },
			[](BoxColliderComponent& c, bool enabled) { c.getColliderItem().enabled = enabled; }));
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
