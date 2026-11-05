#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
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
	std::filesystem::path resolved(std::u8string(path.begin(), path.end()));
	// Абсолютные встроенные ресурсы не зависят от каталога открытого проекта.
	if (!projectDirectory.empty() && !resolved.is_absolute()) {
		if (resolved.has_root_path()) return {};
		resolved = std::filesystem::path(std::u8string(projectDirectory.begin(), projectDirectory.end())) / resolved;
	}
	const auto text = resolved.lexically_normal().generic_u8string();
	return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

namespace {

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

bool MeshResource::SupportsExtension(const std::string& extension)
{
	Assimp::Importer importer;
	return !extension.empty() && importer.IsExtensionSupported(extension);
}

bool MeshResource::load(const std::string& path)
{
	std::wstring wide;
	if (!WidePath(path, wide, loadError)) return false;
	// Importer владеет временной сценой и освобождает её после копирования геометрии.
	Assimp::Importer importer;
	// Отдельная обработка экземпляров корректирует обход граней при зеркальном масштабе.
	importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate |
		aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
		aiProcess_ValidateDataStructure | aiProcess_PreTransformVertices);
	if (!scene) return Fail(loadError, importer.GetErrorString());
	if (!scene->HasMeshes()) return Fail(loadError, "Model contains no meshes");

	// PreTransformVertices запекает трансформации узлов; сохраняем правую систему и CCW.
	MeshData<TexturedVertexType> loaded;
	constexpr size_t maxBytes = (std::numeric_limits<UINT>::max)();
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
		const aiMesh& mesh = *scene->mMeshes[i];
		if (!mesh.HasPositions() || !mesh.HasNormals())
			return Fail(loadError, "Mesh has no positions or normals");
		// Ограничения DX11 проверяем до выделения массивов и сужения индексов.
		if (mesh.mNumVertices > maxBytes / sizeof(TexturedVertexType::Vertex) - loaded.vertices.size() ||
			mesh.mNumFaces > (maxBytes / sizeof(uint32_t) - loaded.indices.size()) / 3)
			return Fail(loadError, "Mesh exceeds DX11 buffer size limits");
		const auto offset = static_cast<uint32_t>(loaded.vertices.size());
		loaded.vertices.reserve(loaded.vertices.size() + mesh.mNumVertices);
		loaded.indices.reserve(loaded.indices.size() + size_t(mesh.mNumFaces) * 3);
		for (unsigned int v = 0; v < mesh.mNumVertices; ++v) {
			const aiVector3D& p = mesh.mVertices[v];
			aiVector3D n = mesh.mNormals[v];
			// Берём первый UV-канал; модели без развёртки получают нулевые UV.
			const aiVector3D uv = mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][v] : aiVector3D(0, 0, 0);
			// Неконечные координаты не должны попадать в GPU, picking и границы выбора.
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
				!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ||
				!std::isfinite(uv.x) || !std::isfinite(uv.y))
				return Fail(loadError, "Mesh contains non-finite vertex data");
			n.NormalizeSafe();
			loaded.vertices.push_back(TexturedVertexType::make({ p.x, p.y, p.z },
				{ n.x, n.y, n.z }, { uv.x, uv.y }));
		}
		for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
			const aiFace& face = mesh.mFaces[f];
			if (face.mNumIndices != 3) return Fail(loadError, "Only triangle meshes are supported");
			for (unsigned int j = 0; j < 3; ++j) {
				if (face.mIndices[j] >= mesh.mNumVertices) return Fail(loadError, "Invalid mesh index");
				loaded.indices.push_back(offset + face.mIndices[j]);
			}
		}
	}
	// Частично загруженная геометрия не становится доступной компонентам.
	if (!loaded.IsValid()) return Fail(loadError, "Model contains no valid triangle geometry");
	meshData = std::move(loaded);
	return true;
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
	if (!std::isfinite(specularStrength) || specularStrength < 0.0f)
		return Fail(error, "specular_strength: expected a finite nonnegative number");
	if (!std::isfinite(shininess) || shininess <= 0.0f)
		return Fail(error, "shininess: expected a finite positive number");

	// Ссылка на текстуру переносима между папками проекта и не выходит из его корня.
	if (!diffuse.empty()) {
		std::wstring wide;
		if (!WidePath(diffuse, wide, error)) return false;
		const std::filesystem::path relative(wide);
		if (relative.has_root_path() || wide.find_first_of(L":*?\"<>|") != std::wstring::npos)
			return Fail(error, "diffuse: expected a path relative to the resource root");
		for (const auto& part : relative)
			if (part == L"..") return Fail(error, "diffuse: parent directory traversal is not allowed");
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
		if (name != "version" && name != "shader" && name != "render_state" &&
			name != "sampler" && name != "diffuse" && name != "specular_strength" && name != "shininess")
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
	const auto diffuseField = json.FindMember("diffuse");
	if (diffuseField != json.MemberEnd() && !diffuseField->value.IsNull()) {
		if (!ReadString(json, "diffuse", loaded.diffuse, error)) return false;
		if (loaded.diffuse.empty()) return Fail(error, "diffuse: use null instead of an empty path");
	}
	if (!loaded.Validate(error)) return false;
	*this = std::move(loaded);
	return true;
}

bool MaterialData::Save(const std::string& path, std::string& error) const
{
	if (!Validate(error)) return false;
	std::wstring wide;
	if (!WidePath(path, wide, error)) return false;
	if (diffuse.size() > (std::numeric_limits<rapidjson::SizeType>::max)())
		return Fail(error, "diffuse: path is too long");

	// PrettyWriter экранирует строки и записывает числа независимо от локали.
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("version"); writer.Int(version);
	writer.Key("shader"); writer.String(shader.c_str());
	writer.Key("render_state"); writer.String(renderState.c_str());
	writer.Key("sampler"); writer.String(sampler.c_str());
	writer.Key("diffuse");
	if (diffuse.empty()) writer.Null();
	else writer.String(diffuse.data(), static_cast<rapidjson::SizeType>(diffuse.size()));
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
	if (!device) return Fail(loadError, "DX11 device is null");
	if (!data.Load(path, loadError)) return false;
	if (standardResourceDirectory.empty())
		return Fail(loadError, "Standard resource directory is not configured");
	const auto& directory = standardResourceDirectory;
	const std::filesystem::path standardPath(std::u8string(
		reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
	const auto shaderPath = standardPath /
		"Shaders" / "Simple.hlsl";
	shader = ShaderPack::Create(device, shaderPath.native(), TexturedVertexType::Layout());
	if (!shader.IsValid()) return Fail(loadError, "Cannot create builtin/simple shader");

	state = data.renderState == "wireframe"
		? RenderState::CreateWireframe(*device) : RenderState::CreateOpaque(*device);
	if (!state.Raster() || !state.Depth() || !state.Blend())
		return Fail(loadError, "Cannot create material render state");
	material = Material(shader, state);
	if (!data.diffuse.empty()) {
		if (!resources || resources->GetResourceDirectory().empty())
			return Fail(loadError, "Resource manager and resource directory are required for diffuse textures");
		// Ссылки материала считаются от res, независимо от расположения самого JSON.
		std::wstring root, relative;
		if (!WidePath(resources->GetResourceDirectory(), root, loadError) ||
			!WidePath(data.diffuse, relative, loadError)) return false;
		const auto texturePath = (std::filesystem::path(root) / relative).generic_u8string();
		auto* texture = resources->load<TextureResource>(
			std::string(reinterpret_cast<const char*>(texturePath.data()), texturePath.size()), device);
		if (!texture) return Fail(loadError, resources->GetLastError());

		// Все допустимые значения sampler преобразуются в существующие DX11-описания.
		const auto& description = data.sampler == "wrap_linear" ? SamplerDescs::WrapLinear :
			data.sampler == "wrap_point" ? SamplerDescs::WrapPoint :
			data.sampler == "clamp_linear" ? SamplerDescs::ClampLinear : SamplerDescs::ClampPoint;
		if (FAILED(device->CreateSamplerState(&description, &samplerState)))
			return Fail(loadError, "Cannot create material sampler");
		material = Material(shader, texture->GetTexture(), samplerState, state);
	}
	material.specularStrength = data.specularStrength;
	material.shininess = data.shininess;
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
	if (!BuildTexture(sourcePath, options, candidate, error) || !options.Save(sourcePath, error)) return false;
	// Материалы заимствуют адрес m_texture: заменяется содержимое, а не объект ресурса.
	m_texture = std::move(candidate);
	settings = options;
	return true;
}
