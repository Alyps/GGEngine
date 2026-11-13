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

struct MaterialData;

class ResourceManager {
private:
	std::unordered_map<std::string, Resource*> resources;
	std::string lastError;
	std::string standardResourceDirectory = "res/StandartResources";
	std::string projectDirectory;
	std::string resourceDirectory = "res";

public:
	explicit ResourceManager(std::string directory = {}) : projectDirectory(std::move(directory)) {}
	// Пустой корень сохраняет поведение standalone: пути от рабочей папки процесса.
	std::string ResolvePath(const std::string& path) const;
	// После копирования проекта меняем корень будущих загрузок; живые ресурсы остаются в кеше.
	void SetProjectDirectory(std::string directory) { projectDirectory = std::move(directory); }
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
	bool ApplyMaterial(const std::string& path, const MaterialData& options, ID3D11Device* device);

	// The returned pointer is borrowed and remains valid until this manager is destroyed.
	// Constructor arguments are used only on a cache miss. Paths are UTF-8.
	template<typename T, typename... Args>
	T* load(const std::string& path, Args&&... args)
	{
		static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");
		static_assert(!std::is_abstract_v<T>, "T must be a concrete resource type");

		lastError.clear();
		if (path.empty()) { lastError = "Resource path is empty"; return nullptr; }
		const std::string resolved = ResolvePath(path);
		if (resolved.empty()) { lastError = "Invalid resource path"; return nullptr; }
		const auto found = resources.find(resolved);
		if (found != resources.end()) {
			T* cached = dynamic_cast<T*>(found->second);
			if (!cached) lastError = path + ": cached resource has a different type";
			return cached;
		}

		T* resource = new (std::nothrow) T(std::forward<Args>(args)...);
		if (!resource) { lastError = "Cannot allocate resource: " + path; return nullptr; }
		Resource* base = resource;
		if (!base->load(resolved)) {
			lastError = path + ": " + (base->loadError.empty() ? "Cannot load resource" : base->loadError);
			delete base;
			return nullptr;
		}

		resources.emplace(resolved, base);
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

// Пути карт UTF-8 относительно корня ресурсов; пустые слоты необязательны.
struct MaterialData {
	int version = 1;
	std::string shader = "builtin/simple";
	std::string renderState = "opaque";
	std::string sampler = "wrap_linear";
	std::string diffuse;
	float specularStrength = 0.5f;
	float shininess = 32.0f;
	std::string normal;
	std::string ao;

	bool operator==(const MaterialData&) const = default;
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
	bool Build(MaterialData options, std::string& error);
	void Adopt(MaterialResource& candidate);

public:
	~MaterialResource() override;
	const MaterialData& GetData() const { return data; }
	Material& GetMaterial() { return material; }
	const Material& GetMaterial() const { return material; }
	// Сохраняем актуальные параметры блика вместе с исходными ссылками.
	bool Save(const std::string& path, std::string& error) const;
};

// Необязательный файл image.png.texture.json; отсутствие сохраняет прежнюю загрузку.
struct TextureSettings {
	enum class ColorSpace { Auto, Linear, SRGB };
	enum class Mipmaps { Auto, Disabled };
	ColorSpace colorSpace = ColorSpace::Auto;
	Mipmaps mipmaps = Mipmaps::Auto;
	bool operator==(const TextureSettings&) const = default;
	bool Validate(std::string& error) const;
	bool Load(const std::string& imagePath, std::string& error);
	bool Save(const std::string& imagePath, std::string& error) const;
};

class TextureResource final : public Resource {
friend class ResourceManager;

private:
	ComPtr<ID3D11Device> m_device;
	Texture m_texture;
	// Материалы заимствуют это представление, а вторая копия нужна только для sRGB.
	Texture linearTexture;
	TextureSettings settings;
	std::string sourcePath;
	bool BuildTexture(const std::string& path, const TextureSettings& options, Texture& result, std::string& error) const;
	bool BuildLinearTexture(const Texture& primary, TextureSettings options, Texture& result, std::string& error) const;

	explicit TextureResource(ID3D11Device* device) : m_device(device) {}
	bool load(const std::string& path) override;

public:
	const Texture& GetTexture() const { return m_texture; }
	bool RequireLinearTexture(std::string& error);
	const Texture& GetLinearTexture() const { return linearTexture; }
	const TextureSettings& GetSettings() const { return settings; }
	// Сначала готовим GPU-ресурс и сохраняем JSON; только затем заменяем содержимое на прежнем адресе.
	bool ApplySettings(const TextureSettings& options, std::string& error);
};
