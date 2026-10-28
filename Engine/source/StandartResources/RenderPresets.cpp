#include "StandartResources/RenderPresets.h"
#include "ResourceManager.h"

#include <cmath>
#include <filesystem>
#include <numbers>

namespace {

	bool ValidateSize(float value)
	{
		if (!std::isfinite(value) || value <= 0.0f) {
			std::cerr << "Mesh preset dimensions must be finite and positive\n";
			return false;
		}
		return true;
	}

	constexpr DirectX::XMFLOAT4 white{ 1.0f, 1.0f, 1.0f, 1.0f };

	// Вершины перечислены против часовой стрелки при взгляде снаружи.
	void AddFace(MeshData<NormalVertexType>& mesh, DirectX::XMFLOAT3 normal,
		DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b,
		DirectX::XMFLOAT3 c, DirectX::XMFLOAT3 d)
	{
		const auto base = static_cast<uint32_t>(mesh.vertices.size());
		for (const auto position : { a, b, c, d })
			mesh.vertices.push_back(NormalVertexType::make(position, normal, white));
		mesh.indices.insert(mesh.indices.end(), {
			base, base + 1, base + 2, base, base + 2, base + 3
			});
	}

} // namespace

namespace MeshPresets {

	MeshData<NormalVertexType> CreateQuad(float height, float width)
	{
		MeshData<NormalVertexType> mesh;
		if (!ValidateSize(height) || !ValidateSize(width)) return mesh;
		const float x = width * 0.5f;
		const float y = height * 0.5f;
		AddFace(mesh, { 0, 0, 1 },
			{ -x, -y, 0 }, { x, -y, 0 }, { x, y, 0 }, { -x, y, 0 });
		return mesh;
	}

	MeshData<NormalVertexType> CreateRectangle(float height, float width, float length)
	{
		MeshData<NormalVertexType> mesh;
		if (!ValidateSize(height) || !ValidateSize(width) || !ValidateSize(length)) return mesh;
		const float x = width * 0.5f;
		const float y = height * 0.5f;
		const float z = length * 0.5f;
		mesh.vertices.reserve(24);
		mesh.indices.reserve(36);

		// Отдельные вершины каждой грани сохраняют резкие рёбра нормалей.
		AddFace(mesh, { 0, 0, 1 },
			{ -x, -y, z }, { x, -y, z }, { x, y, z }, { -x, y, z });
		AddFace(mesh, { 0, 0, -1 },
			{ x, -y, -z }, { -x, -y, -z }, { -x, y, -z }, { x, y, -z });
		AddFace(mesh, { 1, 0, 0 },
			{ x, -y, z }, { x, -y, -z }, { x, y, -z }, { x, y, z });
		AddFace(mesh, { -1, 0, 0 },
			{ -x, -y, -z }, { -x, -y, z }, { -x, y, z }, { -x, y, -z });
		AddFace(mesh, { 0, 1, 0 },
			{ -x, y, z }, { x, y, z }, { x, y, -z }, { -x, y, -z });
		AddFace(mesh, { 0, -1, 0 },
			{ -x, -y, -z }, { x, -y, -z }, { x, -y, z }, { -x, -y, z });
		return mesh;
	}

	MeshData<NormalVertexType> CreateCircle(float radius)
	{
		MeshData<NormalVertexType> mesh;
		if (!ValidateSize(radius)) return mesh;
		constexpr uint32_t segments = 32;
		constexpr uint32_t stacks = 16;
		constexpr float pi = std::numbers::pi_v<float>;
		mesh.vertices.reserve(2 + (stacks - 1) * segments);
		mesh.indices.reserve(6 * segments * (stacks - 1));

		auto addVertex = [&](DirectX::XMFLOAT3 normal) {
			mesh.vertices.push_back(NormalVertexType::make(
				{ radius * normal.x, radius * normal.y, radius * normal.z }, normal, white));
			};
		addVertex({ 0, 1, 0 });
		for (uint32_t stack = 1; stack < stacks; ++stack) {
			const float latitude = pi * stack / stacks;
			const float ringRadius = std::sin(latitude);
			for (uint32_t segment = 0; segment < segments; ++segment) {
				const float longitude = 2.0f * pi * segment / segments;
				addVertex({ ringRadius * std::cos(longitude), std::cos(latitude),
					ringRadius * std::sin(longitude) });
			}
		}
		const auto bottom = static_cast<uint32_t>(mesh.vertices.size());
		addVertex({ 0, -1, 0 });

		// Полюса имеют по одной вершине, поэтому вырожденных треугольников нет.
		for (uint32_t segment = 0; segment < segments; ++segment) {
			const uint32_t next = (segment + 1) % segments;
			mesh.indices.insert(mesh.indices.end(), { 0, 1 + next, 1 + segment });
			for (uint32_t ring = 0; ring < stacks - 2; ++ring) {
				const uint32_t a = 1 + ring * segments + segment;
				const uint32_t b = 1 + ring * segments + next;
				const uint32_t c = a + segments;
				const uint32_t d = b + segments;
				mesh.indices.insert(mesh.indices.end(), { a, b, c, b, d, c });
			}
			const uint32_t lastRing = 1 + (stacks - 2) * segments;
			mesh.indices.insert(mesh.indices.end(), { bottom, lastRing + segment, lastRing + next });
		}
		return mesh;
	}

} // namespace MeshPresets


namespace RenderItemPresets {

	RenderItem* ImportPreset(ID3D11Device* device, const MeshData<NormalVertexType>& meshData,
		ResourceManager& resources)
	{
		if (resources.GetStandardResourceDirectory().empty()) {
			std::cerr << "ImportPreset: standard resource directory is not configured\n";
			return nullptr;
		}
		auto mesh = Mesh::Create(device, meshData);
		if (!mesh.IsValid()) return nullptr;
		const auto& directory = resources.GetStandardResourceDirectory();
		const std::filesystem::path standardPath(std::u8string(
			reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
		const auto path = (standardPath / "Default.material.json").u8string();
		const std::string utf8Path(reinterpret_cast<const char*>(path.data()), path.size());
		auto* resource = resources.load<MaterialResource>(
			utf8Path, device, resources.GetStandardResourceDirectory());
		if (!resource) {
			std::cerr << "ImportPreset: " << resources.GetLastError() << '\n';
			return nullptr;
		}

		// Материал остаётся у менеджера; вызывающему коду передаём только mesh и item.
		auto* meshPtr = new (std::nothrow) Mesh(std::move(mesh));
		if (!meshPtr) return nullptr;
		auto* item = new (std::nothrow) RenderItem{ meshPtr, &resource->GetMaterial() };
		if (!item) delete meshPtr;
		return item;
	}

	RenderItem* CreateSimple(ID3D11Device* device, const MeshData<NormalVertexType>& meshData,
		bool is_wire_frame, const std::string& standardResourceDirectory)
	{
		if (!device || standardResourceDirectory.empty())
			return nullptr;

		auto mesh = Mesh::Create(device, meshData);
		if (!mesh.IsValid())
			return nullptr;

		const auto& directory = standardResourceDirectory;
		const std::filesystem::path standardPath(std::u8string(
			reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
		const auto shaderPath = standardPath /
			"Shaders" / "Simple.hlsl";
		auto shader = ShaderPack::Create(device, shaderPath.wstring(), mesh.Layout());
		if (!shader.IsValid())
			return nullptr;

		auto state = is_wire_frame
			? RenderState::CreateWireframe(*device)
			: RenderState::CreateOpaque(*device);
		if (!state.Raster() || !state.Depth() || !state.Blend())
			return nullptr;

		Mesh* meshPtr = nullptr;
		ShaderPack* shaderPtr = nullptr;
		RenderState* statePtr = nullptr;
		Material* material = nullptr;
		
		meshPtr = new Mesh(std::move(mesh));
		shaderPtr = new ShaderPack(std::move(shader));
		statePtr = new RenderState(std::move(state));
		material = new Material(*shaderPtr, *statePtr);
		return new RenderItem{ meshPtr, material };
	}

}
