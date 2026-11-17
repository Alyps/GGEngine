#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/ProgressHandler.hpp>
#include <functional>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

// Разбираем заголовки JSON до Windows.h, чтобы макросы min/max не мешали библиотеке.
#include "ResourceManager.h"

std::string ResourceManager::ResolvePath(const std::string& path) const
{
	if (path.empty() || path.find('\0') != std::string::npos
		|| path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())
		|| !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0)) return {};
	std::string text = path;
	std::replace(text.begin(), text.end(), '\\', '/');
	const bool resource = text.starts_with("res://");
	if (resource) text.erase(0, 6);
	std::filesystem::path resolved(std::u8string(text.begin(), text.end()));
	if (resource) {
		// URI не допускает абсолютный хвост, другой протокол и выход через .. .
		if (text.empty() || resolved.has_root_path() || text.find_first_of(":*?\"<>|") != std::string::npos) return {};
		for (const auto& part : resolved) if (part == "..") return {};
		resolved = std::filesystem::path(std::u8string(resourceDirectory.begin(), resourceDirectory.end())) / resolved;
	}
	// Абсолютные служебные ресурсы движка сохраняют прежнее поведение.
	if (!projectDirectory.empty() && !resolved.is_absolute()) {
		if (resolved.has_root_path()) return {};
		resolved = std::filesystem::path(std::u8string(projectDirectory.begin(), projectDirectory.end())) / resolved;
	}
	const auto utf8 = resolved.lexically_normal().generic_u8string();
	return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string ResourceManager::ResourcePath(const std::string& path) const
{
	if (path.empty()) return {};
	const auto resolved = ResolvePath(path), root = ResolvePath(resourceDirectory);
	if (resolved.empty() || root.empty()) return {};
	std::error_code error;
	const auto target = std::filesystem::absolute(std::filesystem::path(std::u8string(resolved.begin(), resolved.end())), error).lexically_normal();
	if (error) return {};
	const auto base = std::filesystem::absolute(std::filesystem::path(std::u8string(root.begin(), root.end())), error).lexically_normal();
	if (error) return {};
	const auto relative = target.lexically_relative(base);
	if (relative.empty() || relative == "." || relative.has_root_path()) return {};
	for (const auto& part : relative) if (part == "..") return {};
	const auto utf8 = relative.generic_u8string();
	const std::string result = "res://" + std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	return ResolvePath(result).empty() ? std::string{} : result;
}

namespace {

	struct MaterialMapField {
		const char* name;
		std::string MaterialData::* path;
		bool writeNull;
	};

	// Формат версии 1 сохраняет пустой diffuse как null и пропускает пустые карты данных.
	constexpr MaterialMapField materialMapFields[] = {
		{ "diffuse", &MaterialData::diffuse, true },
		{ "normal", &MaterialData::normal, false },
		{ "ao", &MaterialData::ao, false }
	};

	bool Fail(std::string& error, const std::string& message)
	{
		error = message;
		return false;
	}

	// WinAPI получает UTF-16; ошибочный UTF-8 и встроенный NUL не допускаются.
	bool WidePath(const std::string& path, std::wstring& result, std::string& error)
	{
		if (path.empty() || path.find('\0') != std::string::npos ||
			path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
			return Fail(error, "Invalid or empty UTF-8 path");
		const int length = static_cast<int>(path.size());
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			path.data(), length, nullptr, 0);
		if (!count) return Fail(error, "Invalid UTF-8 path");
		result.resize(count);
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			path.data(), length, result.data(), count) != count)
			return Fail(error, "Cannot convert UTF-8 path");
		return true;
	}

	bool ReadString(const rapidjson::Document& json, const char* name,
		std::string& value, std::string& error)
	{
		const auto field = json.FindMember(name);
		if (field == json.MemberEnd()) return true;
		if (!field->value.IsString()) return Fail(error, std::string(name) + ": expected a string");
		value.assign(field->value.GetString(), field->value.GetStringLength());
		return true;
	}

	bool ReadFloat(const rapidjson::Document& json, const char* name,
		float& value, std::string& error)
	{
		const auto field = json.FindMember(name);
		if (field == json.MemberEnd()) return true;
		if (!field->value.IsNumber()) return Fail(error, std::string(name) + ": expected a number");
		// Проверяем диапазон до сужения double до float.
		const double number = field->value.GetDouble();
		if (!std::isfinite(number) || std::abs(number) > (std::numeric_limits<float>::max)())
			return Fail(error, std::string(name) + ": number is out of float range");
		value = static_cast<float>(number);
		return true;
	}

	bool WriteFileAtomically(const std::wstring& path, const char* bytes,
		size_t size, std::string& error)
	{
		// Уникальный файл рядом с целевым позволяет заменить его после полной записи.
		static std::atomic<unsigned long> sequence{ 0 };
		const std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) +
			L"." + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
		HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return Fail(error, "Cannot create temporary resource file (Win32 " + std::to_string(GetLastError()) + ")");

		DWORD written = 0;
		bool success = size <= (std::numeric_limits<DWORD>::max)() &&
			WriteFile(file, bytes, static_cast<DWORD>(size), &written, nullptr) && written == size;
		if (success) success = FlushFileBuffers(file) != FALSE;
		if (!CloseHandle(file)) success = false;
		if (!success) {
			DeleteFileW(temporary.c_str());
			return Fail(error, "Cannot write resource file; original file was preserved");
		}

		// Не удаляем старый файл заранее: ошибка замены не должна его уничтожить.
		if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			const DWORD code = GetLastError();
			DeleteFileW(temporary.c_str());
			return Fail(error, "Cannot replace resource file (Win32 " + std::to_string(code) + ")");
		}
		return true;
	}

} // namespace

namespace {
	// .mesh v1: шесть uint32 LE (GMSH, версия, формат вершин, топология, числа вершин/индексов).
	// Затем восемь float32 на вершину (position, normal, UV) и uint32-индексы, без padding.
	constexpr uint32_t meshFileMagic = 0x48534D47;
	constexpr size_t meshHeaderSize = 6 * sizeof(uint32_t);
	constexpr size_t meshVertexSize = 8 * sizeof(float);
	static_assert(std::endian::native == std::endian::little);
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

	bool FiniteMeshVertices(const MeshData<TexturedVertexType>& data)
	{
		for (const auto& v : data.vertices) {
			if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z) ||
				!std::isfinite(v.normal.x) || !std::isfinite(v.normal.y) || !std::isfinite(v.normal.z) ||
				!std::isfinite(v.uv.x) || !std::isfinite(v.uv.y)) return false;
		}
		return true;
	}
}

bool MeshDataResource::load(const std::string& path)
{
	loadError.clear();
	std::wstring wide;
	if (!WidePath(path, wide, loadError)) return false;
	std::ifstream input(std::filesystem::path(wide), std::ios::binary | std::ios::ate);
	if (!input) return Fail(loadError, "Cannot open mesh data file");

	// Сверяем полный размер до выделения массивов: счётчики повреждённого файла недостоверны.
	const auto fileSize = input.tellg();
	if (fileSize < static_cast<std::streamoff>(meshHeaderSize)) return Fail(loadError, "Truncated mesh header");
	input.seekg(0);
	uint32_t header[6]{};
	if (!input.read(reinterpret_cast<char*>(header), sizeof(header))) return Fail(loadError, "Cannot read mesh header");
	if (header[0] != meshFileMagic) return Fail(loadError, "Invalid mesh file signature");
	if (header[1] != 1 || header[2] != 1) return Fail(loadError, "Unsupported mesh version or vertex format");

	// Идентификаторы топологии v1 совпадают со значениями D3D11_PRIMITIVE_TOPOLOGY.
	const auto topology = static_cast<D3D11_PRIMITIVE_TOPOLOGY>(header[3]);
	const uint32_t vertexCount = header[4], indexCount = header[5];
	switch (topology) {
	case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: break;
	case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
		if (indexCount % 2) return Fail(loadError, "Invalid line index count");
		break;
	case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
		if (indexCount < 2) return Fail(loadError, "Invalid line strip index count");
		break;
	case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
		if (indexCount % 3) return Fail(loadError, "Invalid triangle index count");
		break;
	case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
		if (indexCount < 3) return Fail(loadError, "Invalid triangle strip index count");
		break;
	default: return Fail(loadError, "Unsupported mesh topology");
	}

	// Ограничения совпадают с MeshData и атомарной записью ресурсов через WinAPI.
	const uint64_t expectedSize = meshHeaderSize + uint64_t(vertexCount) * meshVertexSize + uint64_t(indexCount) * sizeof(uint32_t);
	constexpr auto maxBytes = (std::numeric_limits<UINT>::max)();
	if (!vertexCount || !indexCount || vertexCount > maxBytes / sizeof(TexturedVertexType::Vertex) ||
		indexCount > maxBytes / sizeof(uint32_t) || expectedSize > maxBytes ||
		static_cast<std::streamoff>(fileSize) != static_cast<std::streamoff>(expectedSize))
		return Fail(loadError, "Invalid mesh counts or file size");

	MeshData<TexturedVertexType> loaded;
	loaded.topology = topology;
	loaded.vertices.resize(vertexCount);
	loaded.indices.resize(indexCount);
	// Поля читаются явно, поэтому размещение Vertex в памяти не является форматом файла.
	for (auto& vertex : loaded.vertices) {
		float values[8];
		if (!input.read(reinterpret_cast<char*>(values), sizeof(values))) return Fail(loadError, "Truncated mesh vertices");
		vertex = TexturedVertexType::make({ values[0], values[1], values[2] },
			{ values[3], values[4], values[5] }, { values[6], values[7] });
	}
	if (!input.read(reinterpret_cast<char*>(loaded.indices.data()), static_cast<std::streamsize>(indexCount) * sizeof(uint32_t)))
		return Fail(loadError, "Truncated mesh indices");
	if (!loaded.IsValid() || !FiniteMeshVertices(loaded)) return Fail(loadError, "Invalid mesh geometry");

	// Публикуем только полностью прочитанную геометрию с проверенными индексами и float.
	meshData = std::move(loaded);
	return true;
}

bool MeshDataResource::Save(const std::string& path, const MeshData<TexturedVertexType>& data, std::string& error)
{
	error.clear();
	if (!data.IsValid() || !FiniteMeshVertices(data)) return Fail(error, "Invalid mesh geometry");
	std::wstring wide;
	if (!WidePath(path, wide, error)) return false;

	// Проверяем размер до сужения счётчиков и выделения выходного буфера.
	const uint64_t size = meshHeaderSize + uint64_t(data.vertices.size()) * meshVertexSize + uint64_t(data.indices.size()) * sizeof(uint32_t);
	if (size > (std::numeric_limits<DWORD>::max)()) return Fail(error, "Mesh file exceeds size limit");
	const uint32_t header[] = { meshFileMagic, 1, 1, static_cast<uint32_t>(data.topology),
		static_cast<uint32_t>(data.vertices.size()), static_cast<uint32_t>(data.indices.size()) };
	std::vector<char> bytes(static_cast<size_t>(size));
	std::memcpy(bytes.data(), header, sizeof(header));
	size_t offset = sizeof(header);

	// Пишем только оговорённые поля, без padding структуры и без изменения нормалей.
	for (const auto& v : data.vertices) {
		const float values[] = { v.position.x, v.position.y, v.position.z,
			v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y };
		std::memcpy(bytes.data() + offset, values, sizeof(values));
		offset += sizeof(values);
	}
	std::memcpy(bytes.data() + offset, data.indices.data(), data.indices.size() * sizeof(uint32_t));
	// Неудачная запись сохраняет предыдущий файл; уже загруженный ресурс не обновляется.
	return WriteFileAtomically(wide, bytes.data(), bytes.size(), error);
}


bool MeshDataResource::SupportsExtension(const std::string& extension)
{
	std::string lower = extension;
	for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return lower == ".mesh";
}

namespace {
	// Assimp владеет обработчиком прогресса; флаг отмены принадлежит заданию редактора.
	class ImportProgress final : public Assimp::ProgressHandler {
		const std::atomic<bool>& cancel;
	public:
		explicit ImportProgress(const std::atomic<bool>& value) noexcept : cancel(value) {}
        // Дополняем nothrow-new Assimp парным delete; обычное удаление остаётся в его heap.
        using Assimp::Intern::AllocateFromAssimpHeap::operator delete;
        static void operator delete(void* memory, const std::nothrow_t&) noexcept {
            Assimp::Intern::AllocateFromAssimpHeap::operator delete(memory);
        }
		bool Update(float) override { return !cancel.load(); }
	};

	bool ImportTexture(const aiScene& scene, const std::filesystem::path& directory,
		const std::string& reference, const aiColor4D& factor, bool color, bool ao,
		ImportedModel::Texture& output, std::string& error)
	{
		DirectX::ScratchImage source, converted;
		HRESULT status = E_FAIL;
		if (reference.empty()) {
			// Постоянный цвет становится однопиксельной diffuse-текстурой.
			status = source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
			if (SUCCEEDED(status)) std::memset(source.GetPixels(), 255, source.GetPixelsSize());
		} else if (const auto* embedded = scene.GetEmbeddedTexture(reference.c_str())) {
			if (embedded->mHeight) {
				status = source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, embedded->mWidth, embedded->mHeight, 1, 1);
				if (SUCCEEDED(status)) {
					const auto* image = source.GetImage(0, 0, 0);
					for (size_t y = 0; y < image->height; ++y) for (size_t x = 0; x < image->width; ++x) {
						const auto& pixel = embedded->pcData[y * image->width + x];
						auto* target = image->pixels + y * image->rowPitch + x * 4;
						target[0] = pixel.r; target[1] = pixel.g; target[2] = pixel.b; target[3] = pixel.a;
					}
				}
			} else {
				// Встроенные сжатые изображения декодируются без промежуточного файла.
				const auto* bytes = reinterpret_cast<const uint8_t*>(embedded->pcData);
				status = DirectX::LoadFromWICMemory(bytes, embedded->mWidth, DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, source);
				if (FAILED(status)) status = DirectX::LoadFromDDSMemory(bytes, embedded->mWidth, DirectX::DDS_FLAGS_NONE, nullptr, source);
				if (FAILED(status)) status = DirectX::LoadFromTGAMemory(bytes, embedded->mWidth, DirectX::TGA_FLAGS_NONE, nullptr, source);
			}
		} else {
			std::string normalized = reference;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			const auto file = directory / std::filesystem::path(std::u8string(normalized.begin(), normalized.end()));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec)) {
                const auto utf8 = file.generic_u8string();
                return Fail(error, "Texture file is missing or inaccessible: " +
                    std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
            }
			status = DirectX::LoadFromWICFile(file.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, source);
			if (FAILED(status)) status = DirectX::LoadFromDDSFile(file.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, source);
			if (FAILED(status)) status = DirectX::LoadFromTGAFile(file.c_str(), DirectX::TGA_FLAGS_NONE, nullptr, source);
		}
		if (FAILED(status)) return Fail(error, "Cannot decode texture (unsupported format or damaged image): " + reference);
		const auto* image = source.GetImage(0, 0, 0);
		if (!image) return Fail(error, "Texture has no image: " + reference);
		// Нормализуем формат, затем запекаем цветовой множитель или извлекаем AO из R.
		if (DirectX::IsCompressed(image->format))
			status = DirectX::Decompress(*image, DXGI_FORMAT_R8G8B8A8_UNORM, converted);
		else status = DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0, converted);
		if (image->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			if (FAILED(status)) return Fail(error, "Cannot convert texture: " + reference);
			image = converted.GetImage(0, 0, 0);
		}
		const float factors[] = { factor.r, factor.g, factor.b, factor.a };
		for (float value : factors) if (!std::isfinite(value)) return Fail(error, "Non-finite material color");
		const auto linear = [](float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };
		const auto srgb = [](float x) { return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; };
		for (size_t y = 0; y < image->height; ++y) for (size_t x = 0; x < image->width; ++x) {
			auto* pixel = image->pixels + y * image->rowPitch + x * 4;
			if (ao) { pixel[1] = pixel[2] = pixel[0]; pixel[3] = 255; }
			if (color) for (size_t c = 0; c < 4; ++c) {
				const float value = c < 3 ? srgb(linear(pixel[c] / 255.0f) * (std::max)(0.0f, factors[c]))
					: pixel[c] / 255.0f * factors[c];
				pixel[c] = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
		DirectX::Blob png;
		status = DirectX::SaveToWICMemory(*image, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png);
		if (FAILED(status)) return Fail(error, "Cannot encode imported texture");
		output.png.assign(png.GetBufferPointer(), png.GetBufferPointer() + png.GetBufferSize());
		return true;
	}
}

bool ImportedModel::ReplaceTexture(size_t index, const std::string& path,
    const std::atomic<bool>& cancel, std::string& error)
{
    error.clear();
    if (index >= textures.size() || path.empty()) return Fail(error, "Invalid texture replacement");
    const auto& texture = textures[index];
    Texture candidate;
    const aiScene emptyScene;
    const auto& f = texture.factor;
    // Исходная сцена уже освобождена; замена всегда читается из внешнего файла.
    if (!ImportTexture(emptyScene, {}, path, aiColor4D(f.x, f.y, f.z, f.w),
        texture.color, texture.ao, candidate, error)) return false;
    if (cancel) return Fail(error, "Texture replacement cancelled.");
    textures[index].png = std::move(candidate.png);
    textures[index].replacement = path;
    textures[index].error.clear();
    return true;
}

bool ImportedModel::Read(const std::string& path, const std::atomic<bool>& cancel, std::string& error)
{
	*this = {};
	error.clear();
	Assimp::Importer importer;
	auto* progress = new (std::nothrow) ImportProgress(cancel);
	if (!progress) return Fail(error, "Cannot allocate import progress");
	importer.SetProgressHandler(progress);
	// Не запекаем узлы и не объединяем меши: aiMesh уже содержит ровно один материал.
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
		aiProcess_JoinIdenticalVertices | aiProcess_ValidateDataStructure | aiProcess_FlipUVs);
	if (!scene) return Fail(error, cancel ? "Import cancelled" : importer.GetErrorString());
	if (!scene->mRootNode || !scene->HasMeshes()) return Fail(error, "Model contains no meshes");
	if (scene->HasAnimations()) warnings.push_back("Animations are not imported; the static node pose is used.");
	std::unordered_map<std::string, int> textureCache;
	const auto directory = std::filesystem::path(std::u8string(path.begin(), path.end())).parent_path();
	for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
		if (cancel) return Fail(error, "Import cancelled");
		const auto& source = *scene->mMaterials[i];
		Material material;
		aiString name;
		source.Get(AI_MATKEY_NAME, name);
		material.name = name.length ? name.C_Str() : "Material";
		aiColor4D color(1, 1, 1, 1);
		if (source.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS) source.Get(AI_MATKEY_COLOR_DIFFUSE, color);
		float opacity = 1, roughness = 0, metallic = 0;
		source.Get(AI_MATKEY_OPACITY, opacity);
		if (opacity < 0.999f || color.a < 0.999f) warnings.push_back(material.name + ": transparency is rendered opaque.");
		aiString alphaMode;
		if (source.Get("$mat.gltf.alphaMode", 0, 0, alphaMode) == AI_SUCCESS && std::string(alphaMode.C_Str()) != "OPAQUE")
			warnings.push_back(material.name + ": alpha blending/masking is not supported.");
		int twoSided = 0;
		source.Get(AI_MATKEY_TWOSIDED, twoSided);
		if (twoSided) warnings.push_back(material.name + ": double-sided rendering is not supported.");
		if (source.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
			if (!std::isfinite(roughness)) return Fail(error, "Invalid roughness");
			material.data.shininess = std::clamp(2.0f / (std::max)(roughness * roughness, 0.0078f) - 2.0f, 1.0f, 256.0f);
			warnings.push_back(material.name + ": PBR shading is approximated by the engine's specular material.");
		} else source.Get(AI_MATKEY_SHININESS, material.data.shininess);
		source.Get(AI_MATKEY_METALLIC_FACTOR, metallic);
		material.data.specularStrength = std::clamp(0.5f + metallic * 0.5f, 0.0f, 1.0f);
		material.data.shininess = std::clamp(material.data.shininess, 1.0f, 256.0f);
		// Для каждого поддерживаемого слота извлекаем изображение один раз на вариант преобразования.
		const auto map = [&](aiTextureType primary, aiTextureType fallback, int& slot, bool isColor, bool isAO) {
			aiString reference;
			unsigned uv = 0;
			aiTextureMapMode mode[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
			aiTextureMapping mapping = aiTextureMapping_UV;
			auto type = source.GetTextureCount(primary) ? primary : fallback;
			const bool found = source.GetTexture(type, 0, &reference, &mapping, &uv, nullptr, nullptr, mode) == AI_SUCCESS;
			if (!found && !isColor) return true;
			if (found && (uv != 0 || mapping != aiTextureMapping_UV)) {
				warnings.push_back(material.name + ": texture requires unsupported UV mapping; using the default slot.");
				if (!isColor) return true;
				reference.Clear();
			}
			if (source.GetTextureCount(type) > 1) warnings.push_back(material.name + ": only the first texture layer is used.");
			aiUVTransform transform;
			if (source.Get(AI_MATKEY_UVTRANSFORM(type, 0), transform) == AI_SUCCESS &&
				(transform.mTranslation.x != 0 || transform.mTranslation.y != 0 || transform.mRotation != 0 ||
				 transform.mScaling.x != 1 || transform.mScaling.y != 1))
				warnings.push_back(material.name + ": texture UV transform is not supported.");
			if (isColor && mode[0] == aiTextureMapMode_Clamp && mode[1] == aiTextureMapMode_Clamp) material.data.sampler = "clamp_linear";
			else if (found && (mode[0] != aiTextureMapMode_Wrap || mode[1] != aiTextureMapMode_Wrap))
				warnings.push_back(material.name + ": texture addressing is approximated by one shared sampler.");
			const aiColor4D factor = isColor ? color : aiColor4D(1, 1, 1, 1);
			const std::string key = std::string(reference.C_Str()) + (isColor ? "|color|" : isAO ? "|ao|" : "|data|") +
				std::to_string(factor.r) + "|" + std::to_string(factor.g) + "|" + std::to_string(factor.b) + "|" + std::to_string(factor.a);
			if (const auto cached = textureCache.find(key); cached != textureCache.end()) { slot = cached->second; return true; }
			Texture texture;
			texture.name = "Texture_" + std::to_string(textures.size()) + ".png";
            texture.source = reference.length ? reference.C_Str() : "(generated material color)";
            texture.factor = { factor.r, factor.g, factor.b, factor.a };
            texture.color = isColor; texture.ao = isAO;
            // Ошибка зависимости сохраняется для UI; пустой PNG означает пустой слот при экспорте.
            ImportTexture(*scene, directory, reference.C_Str(), factor, isColor, isAO, texture, texture.error);
			slot = static_cast<int>(textures.size());
			textureCache.emplace(key, slot);
			textures.push_back(std::move(texture));
			return true;
		};
		if (!map(aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, material.diffuse, true, false) ||
			!map(aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, material.normal, false, false) ||
			!map(aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP, material.ao, false, true)) return false;
		aiColor3D emission;
		source.Get(AI_MATKEY_COLOR_EMISSIVE, emission);
		if (emission.r > 0 || emission.g > 0 || emission.b > 0 || source.GetTextureCount(aiTextureType_EMISSIVE))
			warnings.push_back(material.name + ": emission is not supported.");
		if (source.GetTextureCount(aiTextureType_OPACITY)) warnings.push_back(material.name + ": opacity maps are not supported.");
		if (source.GetTextureCount(aiTextureType_METALNESS) || source.GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) ||
			source.GetTextureCount(aiTextureType_UNKNOWN) || source.GetTextureCount(aiTextureType_SPECULAR) || source.GetTextureCount(aiTextureType_HEIGHT))
			warnings.push_back(material.name + ": metallic/roughness, specular or height texture maps are not supported.");
		if (!material.data.Validate(error)) return false;
		materials.push_back(std::move(material));
	}
	for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
		if (cancel) return Fail(error, "Import cancelled");
		const auto& source = *scene->mMeshes[i];
		if (!source.HasPositions() || !source.HasNormals() || source.mMaterialIndex >= materials.size())
			return Fail(error, "Mesh has no positions/normals or references an invalid material");
		if (source.HasBones() || source.mNumAnimMeshes) warnings.push_back("Skinning/morph targets are not imported; static vertex positions are used.");
        constexpr auto maxBytes = (std::numeric_limits<UINT>::max)();
        if (source.mNumVertices > maxBytes / sizeof(TexturedVertexType::Vertex) ||
            source.mNumFaces > maxBytes / (3 * sizeof(uint32_t))) return Fail(error, "Mesh exceeds DX11 buffer limits");
		Mesh mesh;
		mesh.name = source.mName.length ? source.mName.C_Str() : "Mesh";
		mesh.material = source.mMaterialIndex;
		mesh.data.vertices.reserve(source.mNumVertices);
		for (unsigned v = 0; v < source.mNumVertices; ++v) {
			const auto& p = source.mVertices[v];
			const auto n = source.mNormals[v];
			const auto uv = source.HasTextureCoords(0) ? source.mTextureCoords[0][v] : aiVector3D();
			mesh.data.vertices.push_back(TexturedVertexType::make({ p.x, p.y, p.z }, { n.x, n.y, n.z }, { uv.x, uv.y }));
		}
		for (unsigned f = 0; f < source.mNumFaces; ++f) {
			const auto& face = source.mFaces[f];
			if (face.mNumIndices != 3) return Fail(error, "Only triangle model geometry is supported");
			for (unsigned j = 0; j < 3; ++j) mesh.data.indices.push_back(face.mIndices[j]);
		}
		if (!mesh.data.IsValid() || !FiniteMeshVertices(mesh.data)) return Fail(error, "Invalid imported mesh geometry");
		meshes.push_back(std::move(mesh));
	}
	// Сохраняем локальные TRS; shear нельзя точно представить текущим TransformTree.
	std::unordered_map<unsigned, unsigned> mirrored;
	std::function<bool(const aiNode&, int, bool, unsigned, const aiMatrix4x4&)> visit =
        [&](const aiNode& source, int parent, bool mirror, unsigned depth, const aiMatrix4x4& parentWorld) {
		if (cancel) return Fail(error, "Import cancelled");
		if (depth > 128) return Fail(error, "Model hierarchy is too deep");
		aiVector3D scale, position;
		aiQuaternion rotation;
		source.mTransformation.Decompose(scale, rotation, position);
		const aiMatrix4x4 reconstructed(scale, rotation, position);
		for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) {
			const float value = source.mTransformation[r][c];
			if (!std::isfinite(value) || !std::isfinite(reconstructed[r][c]) ||
				std::abs(value - reconstructed[r][c]) > 0.0001f * (std::max)(1.0f, std::abs(value)))
				return Fail(error, "Node transform contains shear or invalid values: " + std::string(source.mName.C_Str()));
		}
        const auto world = parentWorld * source.mTransformation;
        aiVector3D worldScale, worldPosition;
        aiQuaternion worldRotation;
        world.Decompose(worldScale, worldRotation, worldPosition);
        const aiMatrix4x4 worldTRS(worldScale, worldRotation, worldPosition);
        for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c)
            if (!std::isfinite(worldTRS[r][c]) || !std::isfinite(world[r][c]) ||
                std::abs(world[r][c] - worldTRS[r][c]) > 0.0001f * (std::max)(1.0f, std::abs(world[r][c])))
                return Fail(error, "Accumulated node transform contains shear: " + std::string(source.mName.C_Str()));
		if (scale.x == 0 || scale.y == 0 || scale.z == 0) return Fail(error, "Zero-scale node is not supported");
		mirror = mirror != (scale.x * scale.y * scale.z < 0);
		Node node;
		node.name = source.mName.length ? source.mName.C_Str() : "Node";
		node.parent = parent;
		node.position = { position.x, position.y, position.z };
		node.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
		node.scale = { scale.x, scale.y, scale.z };
		for (unsigned i = 0; i < source.mNumMeshes; ++i) {
			unsigned index = source.mMeshes[i];
			if (index >= scene->mNumMeshes) return Fail(error, "Invalid node mesh index");
			if (mirror) {
				auto found = mirrored.find(index);
				if (found == mirrored.end()) {
					Mesh copy = meshes[index];
					copy.name += "_Mirrored";
					for (size_t f = 0; f < copy.data.indices.size(); f += 3) std::swap(copy.data.indices[f + 1], copy.data.indices[f + 2]);
					found = mirrored.emplace(index, static_cast<unsigned>(meshes.size())).first;
					meshes.push_back(std::move(copy));
				}
				index = found->second;
			}
			node.meshes.push_back(index);
		}
		const int index = static_cast<int>(nodes.size());
		nodes.push_back(std::move(node));
		for (unsigned i = 0; i < source.mNumChildren; ++i) if (!visit(*source.mChildren[i], index, mirror, depth + 1, world)) return false;
		return true;
	};
	return visit(*scene->mRootNode, -1, false, 0, aiMatrix4x4());
}

bool MaterialData::Validate(std::string& error) const
{
	error.clear();
	if (version != 1) return Fail(error, "version: only version 1 is supported");
	if (shader != "builtin/simple") return Fail(error, "shader: unknown shader identifier");
	if (renderState != "opaque" && renderState != "wireframe")
		return Fail(error, "render_state: expected opaque or wireframe");
	if (sampler != "wrap_linear" && sampler != "clamp_linear" &&
		sampler != "wrap_point" && sampler != "clamp_point")
		return Fail(error, "sampler: unknown sampler identifier");
	if (!std::isfinite(specularStrength) || specularStrength < Material::MinSpecularStrength ||
		specularStrength > Material::MaxSpecularStrength)
		return Fail(error, "specular_strength: expected a number between 0 and 1");
	if (!std::isfinite(shininess) || shininess < Material::MinShininess || shininess > Material::MaxShininess)
		return Fail(error, "shininess: expected a number between 1 and 256");

	// Ссылка на текстуру переносима между папками проекта и не выходит из его корня.
	for (const auto& field : materialMapFields) {
		const auto& path = this->*field.path;
		if (path.empty()) continue;
		std::wstring wide;
		if (!WidePath(path.starts_with("res://") ? path.substr(6) : path, wide, error)) return false;
		const std::filesystem::path relative(wide);
		if (relative.has_root_path() || wide.find_first_of(L":*?\"<>|") != std::wstring::npos)
			return Fail(error, std::string(field.name) + ": expected a path relative to the resource root");
		for (const auto& part : relative)
			if (part == L"..") return Fail(error, std::string(field.name) + ": parent directory traversal is not allowed");
	}
	return true;
}

bool MaterialData::Load(const std::string& path, std::string& error)
{
	error.clear();
	std::wstring wide;
	if (!WidePath(path, wide, error)) return false;
	std::ifstream input(std::filesystem::path(wide), std::ios::binary);
	if (!input) return Fail(error, "Cannot open material file");
	const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (input.bad()) return Fail(error, "Cannot read material file");
	if (text.find('\0') != std::string::npos) return Fail(error, "Unexpected NUL byte in JSON");

	// Проверка UTF-8 выполняется парсером; синтаксические ошибки возвращаются без исключений.
	rapidjson::Document json;
	json.Parse<rapidjson::kParseValidateEncodingFlag>(text.data(), text.size());
	if (json.HasParseError())
		return Fail(error, "JSON byte " + std::to_string(json.GetErrorOffset()) + ": " +
			rapidjson::GetParseError_En(json.GetParseError()));
	if (!json.IsObject()) return Fail(error, "Material must be a JSON object");

	// Опечатки и повторные поля не должны незаметно менять смысл материала.
	for (auto field = json.MemberBegin(); field != json.MemberEnd(); ++field) {
		const std::string name(field->name.GetString(), field->name.GetStringLength());
		const bool isMap = std::ranges::any_of(materialMapFields, [&](const auto& map) { return name == map.name; });
		if (name != "version" && name != "shader" && name != "render_state" &&
			name != "sampler" && name != "specular_strength" && name != "shininess" && !isMap)
			return Fail(error, "Unknown material field: " + name);
		for (auto previous = json.MemberBegin(); previous != field; ++previous)
			if (previous->name == field->name) return Fail(error, "Duplicate material field: " + name);
	}
	const auto versionField = json.FindMember("version");
	if (versionField == json.MemberEnd() || !versionField->value.IsInt() || versionField->value.GetInt() != 1)
		return Fail(error, "version: expected integer 1");

	// Необязательные поля получают стандартные значения, а не данные прежнего материала.
	MaterialData loaded;
	if (!ReadString(json, "shader", loaded.shader, error) ||
		!ReadString(json, "render_state", loaded.renderState, error) ||
		!ReadString(json, "sampler", loaded.sampler, error) ||
		!ReadFloat(json, "specular_strength", loaded.specularStrength, error) ||
		!ReadFloat(json, "shininess", loaded.shininess, error)) return false;
	// Старые материалы могли содержать значения вне новых пределов.
	loaded.specularStrength = std::clamp(loaded.specularStrength, Material::MinSpecularStrength, Material::MaxSpecularStrength);
	loaded.shininess = std::clamp(loaded.shininess, Material::MinShininess, Material::MaxShininess);
	for (const auto& map : materialMapFields) {
		const auto field = json.FindMember(map.name);
		if (field == json.MemberEnd() || field->value.IsNull()) continue;
		auto& path = loaded.*map.path;
		if (!ReadString(json, map.name, path, error)) return false;
		if (path.empty()) return Fail(error, std::string(map.name) + ": use null instead of an empty path");
	}
	if (!loaded.Validate(error)) return false;
	// Старые карты были относительны корню ресурсов, а не файлу материала.
	for (const auto& map : materialMapFields) {
		auto& reference = loaded.*map.path;
		std::replace(reference.begin(), reference.end(), '\\', '/');
		if (!reference.empty()) {
			if (!reference.starts_with("res://")) reference = "res://" + reference;
			reference = ResourceManager().ResourcePath(reference);
			if (reference.empty()) return Fail(error, "Invalid texture resource path");
		}
	}
	*this = std::move(loaded);
	return true;
}

bool MaterialData::Save(const std::string& path, std::string& error) const
{
	if (!Validate(error)) return false;
	std::wstring wide;
	if (!WidePath(path, wide, error)) return false;
	// PrettyWriter экранирует строки и записывает числа независимо от локали.
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("version"); writer.Int(version);
	writer.Key("shader"); writer.String(shader.c_str());
	writer.Key("render_state"); writer.String(renderState.c_str());
	writer.Key("sampler"); writer.String(sampler.c_str());
	for (const auto& map : materialMapFields) {
		const auto& source = this->*map.path;
		const auto texturePath = source.empty() ? std::string{} : ResourceManager().ResourcePath(
			source.starts_with("res://") ? source : "res://" + source);
		if (!source.empty() && texturePath.empty()) return Fail(error, "Invalid texture resource path");
		if (texturePath.empty() && !map.writeNull) continue;
		writer.Key(map.name);
		if (texturePath.empty()) writer.Null();
		else writer.String(texturePath.data(), static_cast<rapidjson::SizeType>(texturePath.size()));
	}
	writer.Key("specular_strength"); writer.Double(specularStrength);
	writer.Key("shininess"); writer.Double(shininess);
	writer.EndObject();
	buffer.Put('\n');
	return WriteFileAtomically(wide, buffer.GetString(), buffer.GetSize(), error);
}

MaterialResource::MaterialResource(ID3D11Device* sourceDevice, std::string standardDirectory,
	ResourceManager* resourceManager)
	: device(sourceDevice), standardResourceDirectory(std::move(standardDirectory)), resources(resourceManager)
{
	// Ресурс удерживает устройство; собственные DX11-объекты освобождаются полями класса.
	if (device) device->AddRef();
}

MaterialResource::~MaterialResource()
{
	if (samplerState) samplerState->Release();
	if (device) device->Release();
}

bool MaterialResource::load(const std::string& path)
{
	MaterialData options;
	return options.Load(path, loadError) && Build(std::move(options), loadError);
}

bool MaterialResource::Build(MaterialData options, std::string& error)
{
	if (!options.Validate(error)) return false;
	if (!device) return Fail(error, "DX11 device is null");
	data = std::move(options);
	if (standardResourceDirectory.empty())
		return Fail(error, "Standard resource directory is not configured");
	const auto& directory = standardResourceDirectory;
	const std::filesystem::path standardPath(std::u8string(
		reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
	const auto shaderPath = standardPath /
		"Shaders" / "Simple.hlsl";
	shader = ShaderPack::Create(device, shaderPath.native(), TexturedVertexType::Layout());
	if (!shader.IsValid()) return Fail(error, "Cannot create builtin/simple shader");

	state = data.renderState == "wireframe"
		? RenderState::CreateWireframe(*device) : RenderState::CreateOpaque(*device);
	if (!state.Raster() || !state.Depth() || !state.Blend())
		return Fail(error, "Cannot create material render state");
	TextureResource* diffuse = nullptr;
	TextureResource* normal = nullptr;
	TextureResource* ao = nullptr;
	if (!data.diffuse.empty() || !data.normal.empty() || !data.ao.empty()) {
		if (!resources || resources->GetResourceDirectory().empty())
			return Fail(error, "Resource manager and resource directory are required for material textures");
		// Новые карты используют URI; старые строки по-прежнему считаются от корня ресурсов.
		const auto loadMap = [&](const std::string& path, const char* slot, bool linear, TextureResource*& result) {
			if (path.empty()) return true;
			const auto reference = path.starts_with("res://") ? path : "res://" + path;
			auto* texture = resources->load<TextureResource>(reference, device);
			if (!texture) return Fail(error, std::string(slot) + ": " + resources->GetLastError());

			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			texture->GetTexture().SRV()->GetDesc(&view);
			UINT support = 0;
			if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D
				|| FAILED(device->CheckFormatSupport(view.Format, &support)) || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
				return Fail(error, std::string(slot) + ": expected a sampleable 2D texture");
			if (linear && !texture->RequireLinearTexture(error)) return false;
			result = texture;
			return true;
		};
		if (!loadMap(data.diffuse, "Base Color", false, diffuse)
			|| !loadMap(data.normal, "Normal", true, normal)
			|| !loadMap(data.ao, "AO", true, ao)) return false;

		// Все допустимые значения sampler преобразуются в существующие DX11-описания.
		const auto& description = data.sampler == "wrap_linear" ? SamplerDescs::WrapLinear :
			data.sampler == "wrap_point" ? SamplerDescs::WrapPoint :
			data.sampler == "clamp_linear" ? SamplerDescs::ClampLinear : SamplerDescs::ClampPoint;
		if (FAILED(device->CreateSamplerState(&description, &samplerState)))
			return Fail(error, "Cannot create material sampler");
	}
	material = Material(shader, state, samplerState, diffuse ? &diffuse->GetTexture() : nullptr,
		normal ? &normal->GetLinearTexture() : nullptr, ao ? &ao->GetLinearTexture() : nullptr);
	material.specularStrength = data.specularStrength;
	material.shininess = data.shininess;
	return true;
}

void MaterialResource::Adopt(MaterialResource& candidate)
{
	// RenderItem сохраняет адрес material; ссылки внутри него привязываем к новым данным.
	const auto* diffuse = candidate.material.Diffuse();
	const auto* normal = candidate.material.Normal();
	const auto* ao = candidate.material.AmbientOcclusion();
	shader = std::move(candidate.shader);
	state = std::move(candidate.state);
	std::swap(samplerState, candidate.samplerState);
	data = std::move(candidate.data);
	material = Material(shader, state, samplerState, diffuse, normal, ao);
	material.specularStrength = data.specularStrength;
	material.shininess = data.shininess;
}

bool ResourceManager::ApplyMaterial(const std::string& path, const MaterialData& options, ID3D11Device* device)
{
	const auto resolved = ResolvePath(path);
	if (resolved.empty()) return Fail(lastError, "Invalid material path");
	const auto found = resources.find(resolved);
	auto* current = found == resources.end() ? nullptr : dynamic_cast<MaterialResource*>(found->second);
	if (found != resources.end() && !current) return Fail(lastError, "Cached resource has a different type");

	// Загрузка карты может расширить кеш. Удерживаем только адрес ресурса, без итератора.
	MaterialResource candidate(device, standardResourceDirectory, this);
	if (!candidate.Build(options, lastError) || !options.Save(resolved, lastError)) return false;
	if (current) current->Adopt(candidate);
	return true;
}

bool MaterialResource::Save(const std::string& path, std::string& error) const
{
	MaterialData current = data;
	current.specularStrength = material.specularStrength;
	current.shininess = material.shininess;
	return current.Save(path, error);
}

bool TextureSettings::Validate(std::string& error) const
{
	error.clear();
	if (colorSpace != ColorSpace::Auto && colorSpace != ColorSpace::Linear && colorSpace != ColorSpace::SRGB)
		return Fail(error, "Unknown texture color space");
	if (mipmaps != Mipmaps::Auto && mipmaps != Mipmaps::Disabled) return Fail(error, "Unknown mipmap mode");
	return true;
}

bool TextureSettings::Load(const std::string& imagePath, std::string& error)
{
	error.clear();
	std::wstring path;
	if (!WidePath(imagePath + ".texture.json", path, error)) return false;
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		const DWORD code = GetLastError();
		if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) return Fail(error, "Cannot access texture settings");
		*this = {}; return true;
	}
	// Настройки малы; ограничиваем чтение до выделения памяти и не меняем this при ошибке.
	std::error_code code;
	const auto size = std::filesystem::file_size(path, code);
	if (code || size > 65536) return Fail(error, "Texture settings are unavailable or too large");
	std::ifstream input(std::filesystem::path(path), std::ios::binary);
	std::string text(static_cast<size_t>(size), '\0');
	if (!input || !input.read(text.data(), static_cast<std::streamsize>(size))) return Fail(error, "Cannot read texture settings");
	if (text.find('\0') != std::string::npos) return Fail(error, "Unexpected NUL byte in texture settings");
	rapidjson::Document json;
	json.Parse<rapidjson::kParseValidateEncodingFlag>(text.data(), text.size());
	if (json.HasParseError() || !json.IsObject()) return Fail(error, "Invalid texture settings JSON");
	for (auto field = json.MemberBegin(); field != json.MemberEnd(); ++field) {
		const std::string name(field->name.GetString(), field->name.GetStringLength());
		if (name != "version" && name != "color_space" && name != "mipmaps") return Fail(error, "Unknown texture setting: " + name);
		for (auto previous = json.MemberBegin(); previous != field; ++previous)
			if (previous->name == field->name) return Fail(error, "Duplicate texture setting: " + name);
	}
	const auto version = json.FindMember("version");
	if (version == json.MemberEnd() || !version->value.IsInt() || version->value.GetInt() != 1)
		return Fail(error, "Texture settings version must be 1");
	std::string color = "auto", mips = "auto";
	if (!ReadString(json, "color_space", color, error) || !ReadString(json, "mipmaps", mips, error)) return false;
	TextureSettings result;
	if (color == "linear") result.colorSpace = ColorSpace::Linear;
	else if (color == "srgb") result.colorSpace = ColorSpace::SRGB;
	else if (color != "auto") return Fail(error, "color_space: expected auto, linear or srgb");
	if (mips == "disabled") result.mipmaps = Mipmaps::Disabled;
	else if (mips != "auto") return Fail(error, "mipmaps: expected auto or disabled");
	*this = result;
	return true;
}

bool TextureSettings::Save(const std::string& imagePath, std::string& error) const
{
	if (!Validate(error)) return false;
	std::wstring path;
	if (!WidePath(imagePath + ".texture.json", path, error)) return false;
	rapidjson::StringBuffer bytes;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes);
	writer.StartObject(); writer.Key("version"); writer.Int(1);
	writer.Key("color_space"); writer.String(colorSpace == ColorSpace::Auto ? "auto" : colorSpace == ColorSpace::Linear ? "linear" : "srgb");
	writer.Key("mipmaps"); writer.String(mipmaps == Mipmaps::Auto ? "auto" : "disabled"); writer.EndObject();
	return WriteFileAtomically(path, bytes.GetString(), bytes.GetSize(), error);
}

bool TextureResource::BuildTexture(const std::string& path, const TextureSettings& options, Texture& result, std::string& error) const
{
	if (!options.Validate(error)) return false;
	if (!m_device) return Fail(error, "Texture device is unavailable");
	std::wstring wide;
	if (!WidePath(path, wide, error)) return false;
	std::wstring extension = std::filesystem::path(wide).extension().native();
	for (auto& c : extension) c = static_cast<wchar_t>(std::towlower(c));
	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image, mipChain, baseMip;
	HRESULT status;
	if (extension == L".dds") status = DirectX::LoadFromDDSFile(wide.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	else if (extension == L".tga") status = DirectX::LoadFromTGAFile(wide.c_str(), DirectX::TGA_FLAGS_NONE, &metadata, image);
	else if (extension == L".hdr") status = DirectX::LoadFromHDRFile(wide.c_str(), &metadata, image);
	else status = DirectX::LoadFromWICFile(wide.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);
	if (FAILED(status)) return Fail(error, "Cannot load texture image (HRESULT " + std::to_string(status) + ")");
	// Меняем интерпретацию каналов; исходные пиксели и файл изображения не перекодируются.
	DXGI_FORMAT format = metadata.format;
	if (options.colorSpace == TextureSettings::ColorSpace::Linear) format = DirectX::MakeLinear(format);
	if (options.colorSpace == TextureSettings::ColorSpace::SRGB) {
		format = DirectX::MakeSRGB(format);
		if (!DirectX::IsSRGB(format)) return Fail(error, "This image format has no sRGB variant");
	}
	if (format != metadata.format && !image.OverrideFormat(format)) return Fail(error, "Cannot apply texture color space");
	metadata = image.GetMetadata();
	const DirectX::ScratchImage* source = &image;
	if (options.mipmaps == TextureSettings::Mipmaps::Disabled && metadata.mipLevels > 1) {
		// Копируем только базовый mip каждого элемента/среза, сохраняя тип DDS и компрессию.
		auto baseMetadata = metadata; baseMetadata.mipLevels = 1;
		if (FAILED(baseMip.Initialize(baseMetadata))) return Fail(error, "Cannot allocate base mip image");
		for (size_t item = 0; item < metadata.arraySize; ++item) for (size_t slice = 0; slice < metadata.depth; ++slice) {
			const auto* from = image.GetImage(0, item, slice);
			const auto* to = baseMip.GetImage(0, item, slice);
			if (!from || !to || from->rowPitch != to->rowPitch || from->slicePitch != to->slicePitch)
				return Fail(error, "Cannot copy base mip image layout");
			std::memcpy(to->pixels, from->pixels, from->slicePitch);
		}
		source = &baseMip;
	} else if (options.mipmaps == TextureSettings::Mipmaps::Auto && metadata.mipLevels <= 1
		&& metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D) {
		// Auto сохраняет прежнее поведение: готовая цепочка либо генерация с откатом к оригиналу.
		status = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), metadata, DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
		if (SUCCEEDED(status)) source = &mipChain;
	}
	Texture candidate = Texture::Create(m_device.Get(), *source);
	if (!candidate.IsValid()) return Fail(error, "Cannot create GPU texture");
	result = std::move(candidate);
	return true;
}

bool TextureResource::load(const std::string& path)
{
	TextureSettings options;
	if (!options.Load(path, loadError)) return false;
	Texture candidate;
	if (!BuildTexture(path, options, candidate, loadError)) return false;
	m_texture = std::move(candidate);
	settings = options; sourcePath = path;
	return true;
}

bool TextureResource::ApplySettings(const TextureSettings& options, std::string& error)
{
	Texture candidate;
	if (!BuildTexture(sourcePath, options, candidate, error)) return false;
	Texture linearCandidate;
	if (linearTexture.IsValid() && !BuildLinearTexture(candidate, options, linearCandidate, error)) return false;
	if (!options.Save(sourcePath, error)) return false;
	// Материалы заимствуют адрес m_texture: заменяется содержимое, а не объект ресурса.
	m_texture = std::move(candidate);
	linearTexture = std::move(linearCandidate);
	settings = options;
	return true;
}

bool TextureResource::RequireLinearTexture(std::string& error)
{
	return linearTexture.IsValid() || BuildLinearTexture(m_texture, settings, linearTexture, error);
}

bool TextureResource::BuildLinearTexture(const Texture& primary, TextureSettings options, Texture& result, std::string& error) const
{
	if (DirectX::IsSRGB(primary.Format())) {
		options.colorSpace = TextureSettings::ColorSpace::Linear;
		return BuildTexture(sourcePath, options, result, error);
	}
	// Уже линейная карта использует тот же SRV по стабильному для материала адресу Texture.
	result.m_srv = primary.m_srv;
	result.m_width = primary.m_width;
	result.m_height = primary.m_height;
	result.m_format = primary.m_format;
	return true;
}
