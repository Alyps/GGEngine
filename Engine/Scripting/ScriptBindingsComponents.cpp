#include "ScriptBindings.h"
#include "Object.h"
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
		} else if (name == "EditorCamera") {
			if (auto* typed = dynamic_cast<EditorCamera*>(component)) return sol::make_object(lua, typed);
		} else if (name == "CameraMovement") {
			if (auto* typed = dynamic_cast<CameraMovementComponent*>(component)) return sol::make_object(lua, typed);
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
	lua.new_usertype<EditorCamera>("EditorCamera", sol::no_constructor);

	lua.new_usertype<CameraMovementComponent>("CameraMovement",
		sol::no_constructor,
		"speed", sol::property([](const CameraMovementComponent& c) { return c.speed; },
			[](CameraMovementComponent& c, float value) { c.speed = value; }),
		"mouse_sensitivity", sol::property([](const CameraMovementComponent& c) { return c.mouseSensitivity; },
			[](CameraMovementComponent& c, float value) { c.mouseSensitivity = value; }));

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
