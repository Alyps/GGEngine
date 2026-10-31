#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <iostream>
#include <d3d.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include <vector>
#include <type_traits>
#include <cstdint>
#include <limits>
#include <utility>

#include "Window.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")





// ВЕРШИНЫ

struct VertexLayout {
	const D3D11_INPUT_ELEMENT_DESC* elements;
	uint32_t stride;
	uint32_t elementCount;
};

class SimpleVertexType {
public:
	struct Vertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
	};

	static constexpr D3D11_INPUT_ELEMENT_DESC kElements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		  0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,
		  0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	static constexpr uint32_t kStride = sizeof(Vertex);
	static constexpr uint32_t kElementCount = std::size(kElements);

	static constexpr VertexLayout Layout() {
		return { kElements, kStride, kElementCount };
	}

	static constexpr Vertex make(DirectX::XMFLOAT3 p, DirectX::XMFLOAT4 c) {
		return Vertex{ p, c };
	}
};


class NormalVertexType {
public:
	struct Vertex {
		DirectX::XMFLOAT3 position;  // POSITION0
		DirectX::XMFLOAT3 normal;    // NORMAL0
		DirectX::XMFLOAT4 color;     // COLOR0
	};

	static constexpr D3D11_INPUT_ELEMENT_DESC kElements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		  0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,
		  0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,
		  0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	static constexpr uint32_t kStride = sizeof(Vertex);
	static constexpr uint32_t kElementCount = std::size(kElements);

	static constexpr VertexLayout Layout() {
		return { kElements, kStride, kElementCount };
	}

	static constexpr Vertex make(DirectX::XMFLOAT3 position,
		DirectX::XMFLOAT3 normal,
		DirectX::XMFLOAT4 color) {
		return Vertex{ position, normal, color };
	}
};


class TexturedVertexType {
public:
	struct Vertex {
		DirectX::XMFLOAT3 position; // POSITION0
		DirectX::XMFLOAT3 normal;   // NORMAL0
		DirectX::XMFLOAT2 uv;       // TEXCOORD0
	};

	static constexpr D3D11_INPUT_ELEMENT_DESC kElements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		  0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,
		  0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
		  0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	static constexpr uint32_t kStride = sizeof(Vertex);
	static constexpr uint32_t kElementCount = std::size(kElements);

	static constexpr VertexLayout Layout() {
		return { kElements, kStride, kElementCount };
	}

	static constexpr Vertex make(DirectX::XMFLOAT3 position,
		DirectX::XMFLOAT3 normal,
		DirectX::XMFLOAT2 uv) {
		return Vertex{ position, normal, uv };
	}
};


//МЕШ

// Данные геометрии в памяти CPU. Layout всегда определяется типом вершин.
template<typename TVertexType>
struct MeshData {
	using Vertex = typename TVertexType::Vertex;
	static_assert(std::is_standard_layout_v<Vertex> && std::is_trivially_copyable_v<Vertex>);
	static_assert(TVertexType::Layout().stride == sizeof(Vertex));

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	static constexpr VertexLayout Layout() { return TVertexType::Layout(); }

	// Поддерживаем индексированные точки, линии и треугольники без strip-cut индексов.
	bool IsValid() const {
		constexpr auto maxBytes = (std::numeric_limits<UINT>::max)();
		if (vertices.empty() || indices.empty() ||
			vertices.size() > maxBytes / sizeof(Vertex) ||
			indices.size() > maxBytes / sizeof(uint32_t))
			return false;

		switch (topology) {
		case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: break;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
			if (indices.size() % 2 != 0) return false;
			break;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
			if (indices.size() < 2) return false;
			break;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			if (indices.size() % 3 != 0) return false;
			break;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			if (indices.size() < 3) return false;
			break;
		default: return false;
		}
		for (const auto index : indices)
			if (index >= vertices.size()) return false;
		return true;
	}
};



// Готовые вершины создаются снаружи через соответствующий VertexType::make.
// Массивы передаются по значению: lvalue копируется, std::move передаёт владение.
// Геометрию можно проверить через MeshData::IsValid(); Mesh::Create проверяет её сам.
// Формат указывается явно: Create<SimpleVertexType>(vertices, indices).
template<typename TVertexType>
MeshData<TVertexType> CreateMeshData(
	std::vector<typename TVertexType::Vertex> vertices,
	std::vector<uint32_t> indices,
	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
{
	return { std::move(vertices), std::move(indices), topology };
}


class Mesh {
public:
private:
	ComPtr<ID3D11Buffer> m_vb;
	ComPtr<ID3D11Buffer> m_ib;

	Mesh() = default;

public:
	// Тип формата выводится из MeshData. При ошибке возвращается невалидный Mesh.
	template<typename TVertexType>
	static Mesh Create(ID3D11Device* device, const MeshData<TVertexType>& data);

	bool IsValid() const { return m_vb && m_ib; }
	D3D11_PRIMITIVE_TOPOLOGY Topology() const { return m_topology; }

	ID3D11Buffer* VertexBuffer() const { return m_vb.Get(); }
	ID3D11Buffer* IndexBuffer()  const { return m_ib.Get(); }

	uint32_t VertexCount() const { return m_vertexCount; }
	uint32_t IndexCount()  const { return m_indexCount; }

	// Метаданные типа вершины — нужны для построения InputLayout
	const VertexLayout& Layout() const { return m_layout; }

private:
	uint32_t m_vertexCount = 0;
	uint32_t m_indexCount = 0;

	VertexLayout m_layout{};
	D3D11_PRIMITIVE_TOPOLOGY m_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
};


template<typename TVertexType>
inline Mesh Mesh::Create(ID3D11Device* device, const MeshData<TVertexType>& data)
{
	if (!device || !data.IsValid()) {
		OutputDebugStringA("Mesh::Create: invalid device or mesh data\n");
		return Mesh{};
	}
	const auto& vertices = data.vertices;
	const auto& indices = data.indices;

	Mesh m;

	D3D11_BUFFER_DESC vbDesc{};
	vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(typename TVertexType::Vertex));
	vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = 0;
	vbDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA vbData{};
	vbData.pSysMem = vertices.data();

	HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, m.m_vb.ReleaseAndGetAddressOf());
	if (FAILED(hr)) {
		return Mesh{};
	}

	D3D11_BUFFER_DESC ibDesc{};
	ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
	ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
	ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibDesc.CPUAccessFlags = 0;
	ibDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA ibData{};
	ibData.pSysMem = indices.data();

	hr = device->CreateBuffer(&ibDesc, &ibData, m.m_ib.ReleaseAndGetAddressOf());
	if (FAILED(hr)) {
		return Mesh{};
	}

	m.m_vertexCount = static_cast<uint32_t>(vertices.size());
	m.m_indexCount = static_cast<uint32_t>(indices.size());
	m.m_layout = data.Layout();
	m.m_topology = data.topology;

	return m;
}



// ШЕЙДЕРЫ



class ShaderPack
{
friend class MaterialResource;
private:
	ComPtr<ID3D11VertexShader> vertexShader;
	ComPtr<ID3D11PixelShader> pixelShader;
	ComPtr<ID3D11InputLayout> layout;

	ShaderPack() = default;

public:
	// Один HLSL с VSMain/PSMain (vs_5_0/ps_5_0). При ошибке возвращается пустой ShaderPack.
	static ShaderPack Create(ID3D11Device* device, const std::wstring& path,
		const VertexLayout& vertexLayout);

	bool IsValid() const { return vertexShader && pixelShader && layout; }

	ID3D11VertexShader* VertexShader() const { return vertexShader.Get(); }
	ID3D11PixelShader* PixelShader()  const { return pixelShader.Get(); }
	ID3D11InputLayout* InputLayout()  const { return layout.Get(); }
};






class RenderState {
friend class MaterialResource;
private:
	ComPtr<ID3D11RasterizerState> m_raster;
	ComPtr<ID3D11DepthStencilState> m_depth;
	ComPtr<ID3D11BlendState> m_blend;

	RenderState() = default;

public:
	// Готовые пресеты
	static RenderState CreateOpaque(ID3D11Device& device);
	static RenderState CreateTransparent(ID3D11Device& device);
	static RenderState CreateWireframe(ID3D11Device& device);
	static RenderState CreateSimple(ID3D11Device& device);

	// Применение к контексту
	//void Bind(ID3D11DeviceContext& ctx) const;

	ID3D11RasterizerState* Raster() const { return m_raster.Get(); }
	ID3D11DepthStencilState* Depth()  const { return m_depth.Get(); }
	ID3D11BlendState* Blend()  const { return m_blend.Get(); }
};




// ТЕКСТУРЫ И СЭМПЛЕРЫ

namespace SamplerDescs {

	// Основной сэмплер для 3D-текстур.
	inline constexpr D3D11_SAMPLER_DESC ClampLinear{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.MinLOD = 0.0f,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};

	// Сэмплер для больших поверхностей, сильно наклонённых к камере.
	inline constexpr D3D11_SAMPLER_DESC ClampAnisotropic{
		.Filter = D3D11_FILTER_ANISOTROPIC,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 16,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.MinLOD = 0.0f,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};

	// Пиксель берётся «как есть». Для пиксельной графики, а также рендер-таргеты, которые нельзя фильтровать (depth, G-buffer, ID-буферы).
	inline constexpr D3D11_SAMPLER_DESC ClampPoint{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.MinLOD = 0.0f,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};

	// Для тайлинга с простой линейной интерполяцией.
	inline constexpr D3D11_SAMPLER_DESC WrapLinear{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		.AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
		.AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
		.AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.MinLOD = 0.0f,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};

	// Для пиксельной интерполяции с тайлингом.
	inline constexpr D3D11_SAMPLER_DESC WrapPoint{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
		.AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
		.AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.MinLOD = 0.0f,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};

	// Сэмплер со сравнением для shadow map (используется через tex.SampleCmp).
	inline constexpr D3D11_SAMPLER_DESC Shadow{
		.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
		.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
		.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = 0.0f,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};

} // namespace SamplerDescs


class Texture {
friend class TextureResource;

private:
	ComPtr<ID3D11ShaderResourceView> m_srv;
	uint32_t                  m_width = 0;
	uint32_t                  m_height = 0;

	Texture() = default;

public:
	static Texture Create(ID3D11Device* device, const DirectX::ScratchImage& image);


	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	Texture(Texture&&) noexcept = default;
	Texture& operator=(Texture&&) noexcept = default;

	ID3D11ShaderResourceView* SRV()     const { return m_srv.Get(); }
	uint32_t                  Width()   const { return m_width; }
	uint32_t                  Height()  const { return m_height; }
	bool                      IsValid() const { return m_srv != nullptr; }
};

// МАТЕРИАЛЫ

class Material {
private:
	ShaderPack* m_shader = nullptr;
	const Texture* m_diffuse = nullptr;
	ID3D11SamplerState* m_sampler = nullptr;
	RenderState* m_state = nullptr;

public:
	// Параметры зеркального блика модели Фонга.
	float specularStrength = 0.5f;
	float shininess = 32.0f;
	Material() = default;

	Material(ShaderPack& shader, RenderState& state)
		: m_shader(&shader), m_state(&state) {}

	Material(ShaderPack& shader, const Texture& diffuse,
		ID3D11SamplerState* sampler, RenderState& state)
		: m_shader(&shader)
		, m_diffuse(&diffuse)
		, m_sampler(sampler)
		, m_state(&state)
	{}

	//void Bind(ID3D11DeviceContext& ctx);

	ShaderPack* Shader()  const { return m_shader; }
	const Texture* Diffuse() const { return m_diffuse; }
	ID3D11SamplerState* Sampler() const { return m_sampler; }
	RenderState* State()   const { return m_state; }
};



// Рендеринг

struct TransformNode;

struct RenderItem {
	Mesh* mesh = nullptr;
	Material* material = nullptr;
	// Узлом владеет объект; он должен жить, пока запись используется рендером.
	const TransformNode* transformNode = nullptr;
};

class CameraComponent;

enum class LightType : uint32_t { Directional, Point };

// Независимое от сцены описание света; компонент владеет записью, RenderServer использует её адрес.
struct LightItem {
	LightType type = LightType::Directional;
	bool enabled = true;
	DirectX::XMFLOAT3 position{ 0, 0, 0 };
	DirectX::XMFLOAT3 direction{ 0, 0, -1 };
	DirectX::XMFLOAT3 color{ 1, 1, 1 };
	float intensity = 1.0f;
	float range = 5.0f;
	DirectX::XMFLOAT4 extraParams{ 0, 0, 0, 0 };
};

// Каждая строка занимает один 16-байтовый регистр HLSL.
struct GPULightData {
	DirectX::XMFLOAT3 position;
	uint32_t type;
	DirectX::XMFLOAT3 direction;
	float range;
	DirectX::XMFLOAT3 color;
	float intensity;
	DirectX::XMFLOAT4 extraParams;
};
static_assert(sizeof(GPULightData) == 64);

class RenderServer {
public:
	RenderServer() = default;
	// Регистрации компонентов относятся только к одному экземпляру сервера.
	RenderServer(const RenderServer&) = delete;
	RenderServer& operator=(const RenderServer&) = delete;
	static constexpr uint32_t MaxLights = 16;
private:
	std::vector<LightItem*> lights;
	bool lightLimitExceeded = false;
	DirectX::XMFLOAT3 ambientColor{ 0.03f, 0.03f, 0.03f };
	// Изменение рендера: камера принадлежит сцене, nullptr отключает её.
	CameraComponent* activeCamera = nullptr;
	// GPU Interface
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;

	//Global Render State
	ComPtr<IDXGISwapChain> swapChain;
	ComPtr<ID3D11RenderTargetView> rtv;
	D3D11_VIEWPORT mainViewport;
	ComPtr<ID3D11Texture2D> depthStencilBuffer;
	ComPtr<ID3D11DepthStencilView> dsv;

	std::vector<RenderItem*> renderList;

	//Per Draw State
	ComPtr<ID3D11Buffer> frameCB;
	ComPtr<ID3D11Buffer> objectCB;
	ComPtr<ID3D11SamplerState> sampler;

	struct FrameCBData {
		DirectX::XMFLOAT4X4 view;
		DirectX::XMFLOAT4X4 projection;
		DirectX::XMFLOAT3 cameraPosition;
		uint32_t lightCount;
		DirectX::XMFLOAT3 ambient;
		float padding = 0.0f;
		GPULightData lights[MaxLights]{};
	} frameCBData;

	struct ObjectCBData {
		DirectX::XMFLOAT4X4 world;
		DirectX::XMFLOAT4X4 normalMatrix;
		DirectX::XMFLOAT4 specular;
	} objectCBData;


public:
	// Компонент сохраняет запись живой до снятия регистрации.
	void addLight(LightItem& light);
	void removeLight(LightItem& light);
	void setAmbientColor(const DirectX::XMFLOAT3& color) { ambientColor = color; }
	void Init(const DisplayWindow32& target_window);
	// Камера должна жить до переключения на другую камеру или nullptr.
	void setCamera(CameraComponent* camera) { activeCamera = camera; }
	CameraComponent* getCamera() const { return activeCamera; }
	ID3D11Device* Device() const { return device.Get(); }
	ID3D11DeviceContext* Context() const { return context.Get(); }
	// Отрисовка в заданную цель не меняет камеру и не вызывает Present.
	void DrawScene(ID3D11RenderTargetView* target, ID3D11DepthStencilView* depth,
		const D3D11_VIEWPORT& viewport);
	bool ResizeMainTarget(UINT width, UINT height);
	void BeginMainPass();
	void Present();

	ComPtr<ID3D11Buffer> CreateConstantBuffer(uint32_t byteWidth) const;

	bool UpdateConstantBuffer(ID3D11Buffer* buffer, const void* data, size_t size); //Пока что такой



	// Очищает кадр, рисует список по порядку и вызывает Present.
	void DrawAll();

	// RenderItem принадлежит вызывающему коду и должен жить, пока зарегистрирован.
	void addItem(RenderItem& item);
	void removeItem(RenderItem& item);

};
