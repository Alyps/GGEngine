#pragma once

#include "RenderServer.h"

class ResourceManager;

namespace MeshPresets {

	// Все фигуры центрированы в (0, 0, 0), используют нормали и белый цвет.
	// Размеры должны быть конечными и положительными, иначе возвращается пустой меш.
	// Пример: auto mesh = Mesh::Create(device, MeshPresets::CreateQuad(2.0f, 3.0f));

	// Прямоугольник в плоскости XY, лицевая сторона направлена по +Z.
	MeshData<NormalVertexType> CreateQuad(float height = 1.0f, float width = 1.0f);

	// Прямоугольный параллелепипед: высота по Y, ширина по X, длина по Z.
	MeshData<NormalVertexType> CreateRectangle(float height = 1.0f, float width = 1.0f, float length = 1.0f);

	// Сфера с гладкими нормалями: 32 сегмента, 16 делений по широте.
	MeshData<NormalVertexType> CreateCircle(float radius = 1.0f);

} // namespace MeshPresets


namespace RenderItemPresets {

	// Заполненные меши используют Phong и Opaque; нормали и обход направлены наружу.
	// Вызывающий код владеет item, mesh, material, shader и state; при ошибке nullptr.
	RenderItem* CreateSimple(ID3D11Device* device, const MeshData<NormalVertexType>& meshData, bool is_wire_frame = false);

	// Читает Default.material.json; режим рендера задаётся в файле материала.
	// Вызывающий код владеет item и mesh, менеджер — материалом и его зависимостями.
	// Менеджер должен жить дольше item и использоваться с одним DX11-устройством.
	RenderItem* ImportPreset(ID3D11Device* device, const MeshData<NormalVertexType>& meshData,
		ResourceManager& resources);
}
