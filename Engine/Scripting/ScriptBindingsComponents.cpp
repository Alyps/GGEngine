#include "ScriptBindings.h"
#include "core.h"
#include <string>
#include <sol/sol.hpp>

namespace {

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
		}
	}
	return sol::make_object(lua, sol::lua_nil);
}

}

void BindComponents(lua_State* L)
{
	sol::state_view lua(L);

	// Компоненты и объекты передаются указателями; владение остаётся у C++.
	lua.new_usertype<GameObject>("GameObject",
		sol::no_constructor,
		"transform", sol::property([](GameObject& o) { return &o.getTransform(); }),
		"get_component", &FindComponent);

	// Mesh пока не имеет настраиваемых полей: тип нужен, чтобы найти и проверить компонент.
	lua.new_usertype<MeshComponent>("Mesh", sol::no_constructor);

	// Свойства камеры пока не доступны скриптам: тип нужен, чтобы найти и проверить компонент.
	lua.new_usertype<GameCamera>("GameCamera", sol::no_constructor);
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
