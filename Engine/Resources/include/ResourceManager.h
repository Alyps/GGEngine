#pragma once
#include "RenderServer.h"
#include <cwctype>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

class Resource {
friend class ResourceManager;

private:
	virtual bool load(const std::string& path) = 0;

protected:
	Resource() = default;
	std::string loadError;

public:
	virtual ~Resource() = default;
	Resource(const Resource&) = delete;
	Resource& operator=(const Resource&) = delete;
};

class ResourceManager {
private:
	std::unordered_map<std::string, Resource*> resources;
	std::string lastError;
	std::string standardResourceDirectory = "res/StandartResources";
	std::string resourceDirectory = "res";

public:
	ResourceManager() = default;
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;
	const std::string& GetLastError() const { return lastError; }
	void SetStandardResourceDirectory(std::string directory) {
		standardResourceDirectory = std::move(directory);
	}
	const std::string& GetStandardResourceDirectory() const { return standardResourceDirectory; }
	// Корень для ссылок внутри материалов; обычные load(path) используют путь напрямую.
	void SetResourceDirectory(std::string directory) { resourceDirectory = std::move(directory); }
	const std::string& GetResourceDirectory() const { return resourceDirectory; }

	// The returned pointer is borrowed and remains valid until this manager is destroyed.
	// Constructor arguments are used only on a cache miss. Paths are UTF-8.
	template<typename T, typename... Args>
	T* load(const std::string& path, Args&&... args)
	{
		static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");
		static_assert(!std::is_abstract_v<T>, "T must be a concrete resource type");

		lastError.clear();
		if (path.empty()) { lastError = "Resource path is empty"; return nullptr; }
		const auto found = resources.find(path);
		if (found != resources.end()) {
			T* cached = dynamic_cast<T*>(found->second);
			if (!cached) lastError = path + ": cached resource has a different type";
			return cached;
		}

		T* resource = new (std::nothrow) T(std::forward<Args>(args)...);
		if (!resource) { lastError = "Cannot allocate resource: " + path; return nullptr; }
		Resource* base = resource;
		if (!base->load(path)) {
			lastError = path + ": " + (base->loadError.empty() ? "Cannot load resource" : base->loadError);
			delete base;
			return nullptr;
		}

		resources.emplace(path, base);
		return resource;
	}

	~ResourceManager()
	{
		for (const auto& entry : resources) {
			delete entry.second;
		}
	}


};

// Менеджер владеет ресурсом, ресурс — CPU-геометрией; компоненты только заимствуют её.
class MeshResource final : public Resource {
friend class ResourceManager;
private:
	MeshData<TexturedVertexType> meshData;
	MeshResource() = default;
	bool load(const std::string& path) override;
public:
	const MeshData<TexturedVertexType>& GetMeshData() const { return meshData; }
	static bool SupportsExtension(const std::string& extension);
};

// Описание не содержит DX11-объектов; пустой diffuse записывается как null.
// Пути UTF-8; diffuse задаётся относительно корня ресурсов будущего проекта.
struct MaterialData {
	int version = 1;
	std::string shader = "builtin/simple";
	std::string renderState = "opaque";
	std::string sampler = "wrap_linear";
	std::string diffuse;
	float specularStrength = 0.5f;
	float shininess = 32.0f;

	bool Validate(std::string& error) const;
	// При ошибке чтения исходное описание остаётся неизменным.
	bool Load(const std::string& path, std::string& error);
	bool Save(const std::string& path, std::string& error) const;
};

// Менеджер владеет ресурсом; материал заимствует его шейдер и состояние.
// builtin/simple использует TexturedVertexType; без diffuse поверхность белая.
class MaterialResource final : public Resource {
friend class ResourceManager;
private:
	ID3D11Device* device = nullptr;
	std::string standardResourceDirectory;
	// Менеджер владеет текстурой; ресурс материала владеет своим сэмплером.
	ResourceManager* resources = nullptr;
	ID3D11SamplerState* samplerState = nullptr;
	MaterialData data;
	ShaderPack shader;
	RenderState state;
	Material material;

	MaterialResource(ID3D11Device* device, std::string standardResourceDirectory,
		ResourceManager* resources = nullptr);
	bool load(const std::string& path) override;

public:
	~MaterialResource() override;
	const MaterialData& GetData() const { return data; }
	Material& GetMaterial() { return material; }
	const Material& GetMaterial() const { return material; }
	// Сохраняем актуальные параметры блика вместе с исходными ссылками.
	bool Save(const std::string& path, std::string& error) const;
};

class TextureResource final : public Resource {
friend class ResourceManager;

private:
	ComPtr<ID3D11Device> m_device;
	Texture m_texture;

	explicit TextureResource(ID3D11Device* device) : m_device(device) {}
	bool load(const std::string& path) override;

public:
	const Texture& GetTexture() const { return m_texture; }
};

inline bool TextureResource::load(const std::string& path)
{
	if (!m_device || path.empty() || path.find('\0') != std::string::npos ||
		path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;

	const int pathLength = static_cast<int>(path.size());
	const int wideLength = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), pathLength, nullptr, 0);
	if (wideLength == 0) return false;
	std::wstring wpath(static_cast<size_t>(wideLength), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		path.data(), pathLength, wpath.data(), wideLength) != wideLength) return false;

	const auto dot = wpath.find_last_of(L'.');
	const auto separator = wpath.find_last_of(L"/\\");
	std::wstring ext;
	if (dot != std::wstring::npos &&
		(separator == std::wstring::npos || dot > separator)) ext = wpath.substr(dot);
	for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));

	DirectX::TexMetadata  metadata{};
	DirectX::ScratchImage image{};


	HRESULT hr = E_FAIL;
	if (ext == L".dds") hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	else if (ext == L".tga") hr = DirectX::LoadFromTGAFile(wpath.c_str(), DirectX::TGA_FLAGS_NONE, &metadata, image);
	else if (ext == L".hdr") hr = DirectX::LoadFromHDRFile(wpath.c_str(), &metadata, image);
	else                     hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);
	if (FAILED(hr)) return false;

	

	const DirectX::ScratchImage* src = &image;
	DirectX::ScratchImage        mipChain;

	if (metadata.mipLevels <= 1 &&
		metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D)
	{
		hr = DirectX::GenerateMipMaps(
			image.GetImages(),
			image.GetImageCount(),
			metadata,
			DirectX::TEX_FILTER_DEFAULT,
			0,
			mipChain);

		if (SUCCEEDED(hr)) {
			src = &mipChain;
		}
		// при неудаче молча используем оригинал
	}

	m_texture = Texture::Create(m_device.Get(), *src);
	return m_texture.IsValid();
}
