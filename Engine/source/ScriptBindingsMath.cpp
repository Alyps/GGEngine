#include "ScriptBindings.h"
#include "GGMath.h"
#include <format>
#include <string>
#include <sol/sol.hpp>

void BindMath(lua_State* L)
{
	sol::state_view lua(L);

	// Vector3: значения в Lua хранятся копией; константы сделаны функциями,
	// чтобы скрипт не мог изменить общий экземпляр.
	lua.new_usertype<DXVector3>("Vector3",
		"new", sol::constructors<DXVector3(), DXVector3(float, float, float)>(),
		"x", sol::property([](const DXVector3& v) { return v.x; },
			[](DXVector3& v, float value) { v.x = value; }),
		"y", sol::property([](const DXVector3& v) { return v.y; },
			[](DXVector3& v, float value) { v.y = value; }),
		"z", sol::property([](const DXVector3& v) { return v.z; },
			[](DXVector3& v, float value) { v.z = value; }),

		// Операторы всегда возвращают новое значение.
		sol::meta_function::addition, [](const DXVector3& a, const DXVector3& b) { return a + b; },
		sol::meta_function::subtraction, [](const DXVector3& a, const DXVector3& b) { return a - b; },
		sol::meta_function::multiplication, sol::overload(
			[](const DXVector3& a, const DXVector3& b) { return a * b; },
			[](const DXVector3& a, float s) { return a * s; },
			[](float s, const DXVector3& a) { return a * s; }),
		sol::meta_function::division, sol::overload(
			[](const DXVector3& a, const DXVector3& b) { return a / b; },
			[](const DXVector3& a, float s) { return a / s; }),
		sol::meta_function::unary_minus, [](const DXVector3& v) { return -v; },
		sol::meta_function::equal_to, [](const DXVector3& a, const DXVector3& b) { return a == b; },
		sol::meta_function::to_string, [](const DXVector3& v) {
			return std::format("Vector3({}, {}, {})", v.x, v.y, v.z);
		},

		"copy", [](const DXVector3& v) { return v; },
		"length", [](const DXVector3& v) { return v.Length(); },
		"length_squared", [](const DXVector3& v) { return v.LengthSquared(); },
		"normalized", [](const DXVector3& v) {
			DXVector3 result;
			v.Normalize(result);
			return result;
		},
		"dot", [](const DXVector3& a, const DXVector3& b) { return a.Dot(b); },
		"cross", [](const DXVector3& a, const DXVector3& b) { return a.Cross(b); },
		"distance", [](const DXVector3& a, const DXVector3& b) { return DXVector3::Distance(a, b); },
		"lerp", [](const DXVector3& a, const DXVector3& b, float t) { return DXVector3::Lerp(a, b, t); },

		"zero", []() { return DXVector3::Zero; },
		"one", []() { return DXVector3::One; },
		"unit_x", []() { return DXVector3::UnitX; },
		"unit_y", []() { return DXVector3::UnitY; },
		"unit_z", []() { return DXVector3::UnitZ; });

	// Quaternion: a * b применяет сначала a, затем b (порядок SimpleMath).
	lua.new_usertype<DXQuaternion>("Quaternion",
		"new", sol::constructors<DXQuaternion(), DXQuaternion(float, float, float, float)>(),
		"x", sol::property([](const DXQuaternion& q) { return q.x; },
			[](DXQuaternion& q, float value) { q.x = value; }),
		"y", sol::property([](const DXQuaternion& q) { return q.y; },
			[](DXQuaternion& q, float value) { q.y = value; }),
		"z", sol::property([](const DXQuaternion& q) { return q.z; },
			[](DXQuaternion& q, float value) { q.z = value; }),
		"w", sol::property([](const DXQuaternion& q) { return q.w; },
			[](DXQuaternion& q, float value) { q.w = value; }),

		sol::meta_function::multiplication, [](const DXQuaternion& a, const DXQuaternion& b) { return a * b; },
		sol::meta_function::equal_to, [](const DXQuaternion& a, const DXQuaternion& b) { return a == b; },
		sol::meta_function::to_string, [](const DXQuaternion& q) {
			return std::format("Quaternion({}, {}, {}, {})", q.x, q.y, q.z, q.w);
		},

		"copy", [](const DXQuaternion& q) { return q; },
		"normalized", [](const DXQuaternion& q) {
			DXQuaternion result;
			q.Normalize(result);
			return result;
		},
		"conjugate", [](const DXQuaternion& q) {
			DXQuaternion result;
			q.Conjugate(result);
			return result;
		},
		"rotate", [](const DXQuaternion& q, const DXVector3& v) { return DXVector3::Transform(v, q); },
		"slerp", [](const DXQuaternion& a, const DXQuaternion& b, float t) {
			return DXQuaternion::Slerp(a, b, t);
		},

		"identity", []() { return DXQuaternion::Identity; },
		// Нулевая ось не задаёт поворот, поэтому возвращаем единичный кватернион.
		"from_axis_angle", [](const DXVector3& axis, float radians) {
			if (axis.LengthSquared() == 0.0f) return DXQuaternion::Identity;
			DXVector3 unit;
			axis.Normalize(unit);
			return DXQuaternion::CreateFromAxisAngle(unit, radians);
		},
		"from_euler", [](float yaw, float pitch, float roll) {
			return DXQuaternion::CreateFromYawPitchRoll(yaw, pitch, roll);
		});

	// Color: компоненты от 0 до 1, альфа по умолчанию 1.
	lua.new_usertype<DXColor>("Color",
		"new", sol::constructors<DXColor(), DXColor(float, float, float), DXColor(float, float, float, float)>(),
		"r", sol::property([](const DXColor& c) { return c.x; },
			[](DXColor& c, float value) { c.x = value; }),
		"g", sol::property([](const DXColor& c) { return c.y; },
			[](DXColor& c, float value) { c.y = value; }),
		"b", sol::property([](const DXColor& c) { return c.z; },
			[](DXColor& c, float value) { c.z = value; }),
		"a", sol::property([](const DXColor& c) { return c.w; },
			[](DXColor& c, float value) { c.w = value; }),
		sol::meta_function::to_string, [](const DXColor& c) {
			return std::format("Color({}, {}, {}, {})", c.x, c.y, c.z, c.w);
		},
		"copy", [](const DXColor& c) { return c; });
}
