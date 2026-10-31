#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "Project.h"
#include "core.h"
#include "RenderPresets.h"
#include "EditorFileUtils.h"
#include <algorithm>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;
using EditorFiles::DiskPath;
using EditorFiles::Utf8Path;
using EditorFiles::ReadJson;
using EditorFiles::WriteJson;
namespace {
    bool Fail(std::string& error, const std::string& text) { error = text; return false; }
    bool ValidText(const std::string& text) {
        return !text.empty() && text.size() < 32768 && text.find('\0') == std::string::npos
            && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
    }
    bool RelativeDirectory(const std::string& text) {
        if (!ValidText(text)) return false;
        const auto path = DiskPath(text);
        if (path.has_root_path()) return false;
        for (const auto& part : path) if (part == "..") return false;
        return true;
    }

}

std::string Project::Root() const { return Utf8Path(DiskPath(filePath).parent_path()); }
std::string Project::ResourceRoot() const { return Utf8Path((DiskPath(Root()) / DiskPath(resourceDirectory)).lexically_normal()); }

bool Project::Open(const std::string& file, Project& result, std::string& error)
{
    error.clear();
    if (!ValidText(file)) return Fail(error, "Invalid project path.");
    std::error_code ec;
    const fs::path path = fs::absolute(DiskPath(file), ec).lexically_normal();
    if (ec || path.extension() != ".ggproject") return Fail(error, "Choose a .ggproject file.");
    rapidjson::Document json;
    if (!ReadJson(path, json, error)) return false;
    if (!json.HasMember("format_version") || !json["format_version"].IsInt() || json["format_version"].GetInt() != 1
        || !json.HasMember("name") || !json["name"].IsString()
        || !json.HasMember("resource_directory") || !json["resource_directory"].IsString()
        || !json.HasMember("startup_scene")) return Fail(error, "Unsupported project format.");
    Project candidate;
    if (!json["startup_scene"].IsNull()) {
        if (!json["startup_scene"].IsString()) return Fail(error, "Invalid startup scene path.");
        candidate.startupScene.assign(json["startup_scene"].GetString(), json["startup_scene"].GetStringLength());
        if (!RelativeDirectory(candidate.startupScene)) return Fail(error, "Startup scene must be a relative project path without parent traversal.");
    }
    candidate.name.assign(json["name"].GetString(), json["name"].GetStringLength());
    candidate.resourceDirectory.assign(json["resource_directory"].GetString(), json["resource_directory"].GetStringLength());
    if (!ValidText(candidate.name) || !RelativeDirectory(candidate.resourceDirectory)) return Fail(error, "Invalid project name or relative resource directory.");
    candidate.filePath = Utf8Path(path);
    if (!fs::is_directory(DiskPath(candidate.ResourceRoot()), ec) || ec) return Fail(error, "Project resource directory is missing or inaccessible.");
    result = std::move(candidate);
    return true;
}

bool Project::Save(std::string& error) const
{
    error.clear();
    if (!ValidText(filePath) || !ValidText(name) || !RelativeDirectory(resourceDirectory)
        || (!startupScene.empty() && !RelativeDirectory(startupScene))) return Fail(error, "Invalid project description.");
    rapidjson::StringBuffer bytes;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes);
    writer.StartObject();
    writer.Key("format_version"); writer.Int(1);
    writer.Key("name"); writer.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
    writer.Key("resource_directory"); writer.String(resourceDirectory.data(), static_cast<rapidjson::SizeType>(resourceDirectory.size()));
    writer.Key("startup_scene");
    if (startupScene.empty()) writer.Null();
    else writer.String(startupScene.data(), static_cast<rapidjson::SizeType>(startupScene.size()));
    writer.EndObject();
    return WriteJson(DiskPath(filePath), bytes, true, error);
}

bool Project::Create(const std::string& parent, const std::string& name, Project& result, std::string& error, const Project* source)
{
    error.clear();
    if (!ValidText(parent) || !ValidText(name) || name == "." || name == ".."
        || name.find_first_of("/\\:<>\"|?*") != std::string::npos || name.back() == '.' || name.back() == ' '
        || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) return Fail(error, "Choose a location and a valid folder name.");
    std::error_code ec;
    fs::path base = fs::absolute(DiskPath(parent), ec);
    if (ec || !fs::is_directory(base, ec) || ec) return Fail(error, "Parent directory is missing or inaccessible.");
    base = fs::weakly_canonical(base, ec);
    if (ec) return Fail(error, "Cannot resolve parent directory: " + ec.message());
    const fs::path root = base / DiskPath(name);
    if (source) {
        const fs::path sourceRoot = fs::weakly_canonical(DiskPath(source->ResourceRoot()), ec);
        if (ec) return Fail(error, "Cannot resolve source resource directory.");
        const DWORD attributes = GetFileAttributesW(DiskPath(source->ResourceRoot()).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Fail(error, "Source resource directory is inaccessible or linked.");
        // Назначение внутри исходных ресурсов привело бы к копированию папки в саму себя.
        std::wstring prefix = sourceRoot.native();
        if (!prefix.empty() && prefix.back() != L'\\') prefix += L'\\';
        const std::wstring target = root.native();
        if (_wcsicmp(target.c_str(), sourceRoot.c_str()) == 0 || _wcsnicmp(target.c_str(), prefix.c_str(), prefix.size()) == 0)
            return Fail(error, "Choose a destination outside the current resource directory.");
    }
    // Новый проект получает только новую папку; существующие папки не изменяются.
    if (!fs::create_directory(root, ec)) return Fail(error, !ec || ec == std::errc::file_exists
        ? "A project or folder with this name already exists. Choose another name."
        : "Cannot create project folder: " + ec.message());
    std::vector<fs::path> created{root};
    std::vector<fs::path> copied;
    const std::string resourceDirectory = source ? source->resourceDirectory : "res";
    bool complete = true;
    for (const char* folder : {"res", "res/Scenes", "res/Models", "res/Materials", "res/Textures", "res/Scripts"}) {
        const fs::path directory = root / folder;
        if (!fs::create_directory(directory, ec)) { complete = false; error = "Cannot create resource folder: " + ec.message(); break; }
        created.push_back(directory);
    }
    if (complete && source) {
        // Копируем файлы до записи манифеста. Ссылки и junction не переносим.
        const fs::path sourceRoot = DiskPath(source->ResourceRoot());
        const fs::path destinationRoot = root / DiskPath(resourceDirectory);
        const auto makeDirectory = [&](const fs::path& directory) {
            fs::path current = root;
            for (const auto& part : directory.lexically_relative(root)) {
                if (part == ".") continue;
                current /= part;
                if (fs::create_directory(current, ec)) created.push_back(current);
                else if (ec) return false;
            }
            return true;
        };
        complete = makeDirectory(destinationRoot);
        fs::recursive_directory_iterator iterator, end;
        if (complete) iterator = fs::recursive_directory_iterator(sourceRoot, ec);
        if (ec) complete = false;
        while (complete && iterator != end) {
            const fs::path file = iterator->path();
            const DWORD attributes = GetFileAttributesW(file.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                error = "Cannot copy resources: linked or inaccessible entry: " + Utf8Path(file);
                complete = false;
                break;
            }
            const fs::path destination = destinationRoot / file.lexically_relative(sourceRoot);
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) complete = makeDirectory(destination);
            else {
                complete = fs::copy_file(file, destination, fs::copy_options::none, ec);
                if (complete) copied.push_back(destination);
            }
            if (complete) { iterator.increment(ec); if (ec) complete = false; }
        }
        if (!complete && error.empty()) error = "Cannot copy project resources: " + ec.message();
    }
    const fs::path manifest = root / (DiskPath(name).native() + L".ggproject");
    if (complete) {
        rapidjson::StringBuffer bytes;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes);
        writer.StartObject();
        writer.Key("format_version"); writer.Int(1);
        writer.Key("name"); writer.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
        writer.Key("resource_directory"); writer.String(resourceDirectory.data(), static_cast<rapidjson::SizeType>(resourceDirectory.size()));
        writer.Key("startup_scene"); writer.Null();
        writer.EndObject();
        complete = WriteJson(manifest, bytes, false, error);
    }
    if (!complete) {
        for (const auto& file : copied) fs::remove(file, ec);
        // Удаляем только созданные пустые папки, без рекурсивного удаления содержимого.
        for (auto it = created.rbegin(); it != created.rend(); ++it) fs::remove(*it, ec);
        return false;
    }
    result.name = name;
    result.filePath = Utf8Path(manifest.lexically_normal());
    result.resourceDirectory = resourceDirectory;
    return true;
}

bool Project::CreateTemporary(Project& result, std::string& error)
{
    fs::path directory;
    if (!EditorFiles::UserDirectory(directory, error)) return false;
    const fs::path drafts = directory / L"Drafts";
    std::error_code ec;
    fs::create_directories(drafts, ec);
    if (ec) return Fail(error, "Cannot create temporary project directory: " + ec.message());
    // Каждому запуску — отдельная папка, чтобы не затереть ресурсы другого редактора.
    const std::string name = "Unsaved-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    return Create(Utf8Path(drafts), name, result, error);
}

bool ProjectHistory::Load(std::string& error)
{
    error.clear();
    entries.clear();
    fs::path directory;
    if (!EditorFiles::UserDirectory(directory, error)) return false;
    const fs::path settings = directory / L"recent-projects.json";
    settingsFile = Utf8Path(settings);
    std::error_code ec;
    if (!fs::exists(settings, ec) && !ec) return true;
    rapidjson::Document json;
    if (!ReadJson(settings, json, error)) return false;
    if (!json.HasMember("recent") || !json["recent"].IsArray()) return Fail(error, "Invalid recent projects list.");
    for (const auto& item : json["recent"].GetArray()) {
        if (!item.IsString()) continue;
        const std::string file(item.GetString(), item.GetStringLength());
        if (ValidText(file) && DiskPath(file).is_absolute() && entries.size() < 15
            && std::find(entries.begin(), entries.end(), file) == entries.end()) entries.push_back(file);
    }
    return true;
}

bool ProjectHistory::Remember(const std::string& file, std::string& error)
{
    error.clear();
    if (settingsFile.empty()) return Fail(error, "Recent projects settings are unavailable.");
    // Windows сравнивает пути без учёта регистра; порядок — от последнего открытого.
    const auto wide = DiskPath(file).native();
    std::erase_if(entries, [&](const std::string& entry) { return _wcsicmp(DiskPath(entry).c_str(), wide.c_str()) == 0; });
    entries.insert(entries.begin(), file);
    if (entries.size() > 15) entries.resize(15);
    std::error_code ec;
    const fs::path settings = DiskPath(settingsFile);
    fs::create_directories(settings.parent_path(), ec);
    if (ec) return Fail(error, "Cannot create editor settings directory: " + ec.message());
    rapidjson::StringBuffer bytes;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes);
    writer.StartObject(); writer.Key("recent"); writer.StartArray();
    for (const auto& entry : entries) writer.String(entry.data(), static_cast<rapidjson::SizeType>(entry.size()));
    writer.EndArray(); writer.EndObject();
    return WriteJson(settings, bytes, true, error);
}

bool Project::PopulateDemoScene(Level& level)
{
	// Уровень владеет объектами и освободит частично созданную сцену при отказе.
	GameObject* objects[6]{};
	for (auto& object : objects) {
		object = level.createObject();
		if (!object) return false;
	}
	auto* cube = objects[0]->createComponent<MeshComponent>();
	auto* floor = objects[1]->createComponent<MeshComponent>(MeshPresets::CreateQuad(5.0f, 7.0f));
	auto* secondCube = objects[2]->createComponent<MeshComponent>(true);
	auto* sphere = objects[3]->createComponent<MeshComponent>(MeshPresets::CreateCircle(0.5f));
	auto* directional = objects[4]->createComponent<DirectionalLightComponent>();
	auto* point = objects[5]->createComponent<PointLightComponent>();
	if (!cube || !floor || !secondCube || !sphere || !directional || !point) return false;

	// Повторяем расстановку прежней демонстрации: два куба, шар и пол XZ.
	objects[1]->getTransform().set_position(DXVector3(0.0f, -0.5f, 0.0f));
	objects[1]->getTransform().set_rotation(
		DXQuaternion::CreateFromAxisAngle(DXVector3::UnitX, -DirectX::XM_PIDIV2));
	objects[2]->getTransform().set_position(DXVector3(-1.5f, 0.0f, 0.0f));
	objects[3]->getTransform().set_position(DXVector3(1.5f, 0.0f, 0.0f));
	cube->bind_to_node(&objects[0]->getTransform());
	floor->bind_to_node(&objects[1]->getTransform());
	secondCube->bind_to_node(&objects[2]->getTransform());
	sphere->bind_to_node(&objects[3]->getTransform());

	// Синий направленный и зелёный точечный свет сохраняют вид исходной сцены.
	objects[4]->getTransform().set_rotation(
		DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0.0f));
	directional->color = { 0.0f, 0.0f, 1.0f };
	directional->intensity = 0.6f;
	directional->bind_to_node(&objects[4]->getTransform());
	objects[5]->getTransform().set_position(DXVector3(2.0f, 2.0f, -2.0f));
	point->color = { 0.0f, 1.0f, 0.0f };
	point->intensity = 12.0f;
	point->range = 5.0f;
	point->bind_to_node(&objects[5]->getTransform());
	return true;
}
