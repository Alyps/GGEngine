#include "ScriptBindings.h"
#include "TransformTree.h"
#include <sol/sol.hpp>

namespace {

// Разложение мировой матрицы; при неудаче возвращаем нейтральные значения.
struct WorldParts {
	DXVector3 position = DXVector3::Zero;
	DXQuaternion rotation = DXQuaternion::Identity;
	DXVector3 scale = DXVector3::One;
};

WorldParts DecomposeWorld(const TransformNode& node)
{
	WorldParts parts;
	DXMatrix world = node.worldMatrix();
	DXVector3 scale, position;
	DXQuaternion rotation;
	if (!world.Decompose(scale, rotation, position)) return parts;
	rotation.Normalize();
	parts.position = position;
	parts.rotation = rotation;
	parts.scale = scale;
	return parts;
}

}

void BindTransform(lua_State* L)
{
	sol::state_view lua(L);

	// Узел принадлежит объекту: Lua держит только указатель и не создаёт и не удаляет узлы.
	// add_child/remove_child не привязаны, чтобы скрипт не мог оставить висящий указатель.
	lua.new_usertype<TransformNode>("Transform",
		sol::no_constructor,

		// Свойства возвращают копии: t.position.x = 1 ничего не меняет, нужно присваивать целиком.
		"position", sol::property([](const TransformNode& n) { return DXVector3(n.get_position()); },
			[](TransformNode& n, const DXVector3& v) { n.set_position(v); }),
		"rotation", sol::property([](const TransformNode& n) { return DXQuaternion(n.get_rotation()); },
			[](TransformNode& n, const DXQuaternion& q) { n.set_rotation(q); }),
		"scale", sol::property([](const TransformNode& n) { return DXVector3(n.get_scale()); },
			[](TransformNode& n, const DXVector3& v) { n.set_scale(v); }),

		"inherit_position", sol::property(&TransformNode::is_inherit_position, &TransformNode::set_inherit_position),
		"inherit_rotation", sol::property(&TransformNode::is_inherit_rotation, &TransformNode::set_inherit_rotation),
		"inherit_scale", sol::property(&TransformNode::is_inherit_scale, &TransformNode::set_inherit_scale),

		// Мировые значения только для чтения.
		"world_position", sol::property([](const TransformNode& n) { return DecomposeWorld(n).position; }),
		"world_rotation", sol::property([](const TransformNode& n) { return DecomposeWorld(n).rotation; }),
		"world_scale", sol::property([](const TransformNode& n) { return DecomposeWorld(n).scale; }),

		"move", sol::overload(
			[](TransformNode& n, const DXVector3& v) { n.move(v); },
			[](TransformNode& n, float x, float y, float z) { n.move(x, y, z); }),
		// Нулевая ось не задаёт поворот.
		"rotate", [](TransformNode& n, const DXVector3& axis, float radians) {
			if (axis.LengthSquared() == 0.0f) return;
			n.rotate(axis, radians);
		},
		// Как и в C++, resize умножает текущий масштаб.
		"resize", [](TransformNode& n, float x, float y, float z) { n.resize(x, y, z); },
		// Совпадение цели с позицией или нулевая ось вверх дали бы NaN, поэтому их пропускаем.
		"look_at", sol::overload(
			[](TransformNode& n, const DXVector3& target) {
				if ((target - n.get_position()).LengthSquared() == 0.0f) return;
				n.look_at(target);
			},
			[](TransformNode& n, const DXVector3& target, const DXVector3& up) {
				if ((target - n.get_position()).LengthSquared() == 0.0f) return;
				if (up.LengthSquared() == 0.0f) return;
				n.look_at(target, up);
			}),

		// Иерархия только для чтения; индексы дочерних узлов начинаются с нуля.
		"parent", sol::property([](const TransformNode& n) { return n.get_parent(); }),
		"child_count", sol::property([](const TransformNode& n) {
			return static_cast<int>(n.get_children().size());
		}),
		"get_child", [](const TransformNode& n, int index) -> TransformNode* {
			const auto& children = n.get_children();
			if (index < 0 || index >= static_cast<int>(children.size())) return nullptr;
			return children[static_cast<size_t>(index)];
		});
}
