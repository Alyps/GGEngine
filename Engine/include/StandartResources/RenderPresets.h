#pragma once

#include "RenderServer.h"

namespace MeshPresets {

	// Все фигуры центрированы в (0, 0, 0), используют нормали и белый цвет.
	// Размеры должны быть конечными и положительными, иначе std::invalid_argument.
	// Пример: auto mesh = Mesh::Create(device, MeshPresets::CreateQuad(2.0f, 3.0f));

	// Прямоугольник в плоскости XY, лицевая сторона направлена по +Z.
	MeshData<NormalVertexType> CreateQuad(float height = 1.0f, float width = 1.0f);

	// Прямоугольный параллелепипед: высота по Y, ширина по X, длина по Z.
	MeshData<NormalVertexType> CreateRectangle(float height = 1.0f, float width = 1.0f, float length = 1.0f);

	// Сфера с гладкими нормалями: 32 сегмента, 16 делений по широте.
	MeshData<NormalVertexType> CreateCircle(float radius = 1.0f);

} // namespace MeshPresets


namespace RenderItemPresets {

	// Caller owns the item, mesh, material, shader and state. Returns nullptr on creation failure.
	RenderItem* CreateSimple(ID3D11Device* device, MeshData<NormalVertexType> meshData, bool is_wire_frame = false);
}
