#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "Project.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace fs = std::filesystem;
namespace {
    fs::path Disk(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }
    std::string Utf8(const fs::path& path) {
        const auto text = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
    bool Fail(std::string& error, const std::string& text) { error = text; return false; }
    bool ValidText(const std::string& text) {
        return !text.empty() && text.size() < 32768 && text.find('\0') == std::string::npos
            && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
    }
    bool RelativeDirectory(const std::string& text) {
        if (!ValidText(text)) return false;
        const auto path = Disk(text);
        if (path.has_root_path()) return false;
        for (const auto& part : path) if (part == "..") return false;
        return true;
    }
    bool ReadJson(const fs::path& path, rapidjson::Document& json, std::string& error) {
        // Ограничиваем размер настроек и манифеста до чтения в память.
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec || size > 1024 * 1024) return Fail(error, "Cannot read JSON file (missing, inaccessible or too large).");
        std::ifstream input(path, std::ios::binary);
        std::string bytes(static_cast<size_t>(size), '\0');
        if (!input || !input.read(bytes.data(), static_cast<std::streamsize>(size))) return Fail(error, "Cannot read JSON file.");
        json.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
        if (json.HasParseError() || !json.IsObject()) return Fail(error, "Invalid JSON document.");
        return true;
    }
    bool WriteJson(const fs::path& path, const rapidjson::StringBuffer& bytes, bool replace, std::string& error) {
        // Собственный временный файл не перезаписывает чужие данные при сбое записи.
        static unsigned counter = 0;
        fs::path temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++counter) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return Fail(error, "Cannot create temporary project/settings file.");
        DWORD written = 0;
        bool complete = WriteFile(file, bytes.GetString(), static_cast<DWORD>(bytes.GetSize()), &written, nullptr)
            && written == bytes.GetSize();
        if (complete) complete = FlushFileBuffers(file) != FALSE;
        const bool closed = CloseHandle(file) != FALSE;
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!complete || !closed || !MoveFileExW(temporary.c_str(), path.c_str(), flags)) {
            DeleteFileW(temporary.c_str());
            return Fail(error, "Cannot save project/settings file.");
        }
        return true;
    }
}

std::string Project::Root() const { return Utf8(Disk(filePath).parent_path()); }
std::string Project::ResourceRoot() const { return Utf8((Disk(Root()) / Disk(resourceDirectory)).lexically_normal()); }

bool Project::Open(const std::string& file, Project& result, std::string& error)
{
    error.clear();
    if (!ValidText(file)) return Fail(error, "Invalid project path.");
    std::error_code ec;
    const fs::path path = fs::absolute(Disk(file), ec).lexically_normal();
    if (ec || path.extension() != ".ggproject") return Fail(error, "Choose a .ggproject file.");
    rapidjson::Document json;
    if (!ReadJson(path, json, error)) return false;
    if (!json.HasMember("format_version") || !json["format_version"].IsInt() || json["format_version"].GetInt() != 1
        || !json.HasMember("name") || !json["name"].IsString()
        || !json.HasMember("resource_directory") || !json["resource_directory"].IsString()
        || !json.HasMember("startup_scene")) return Fail(error, "Unsupported project format.");
    // Пока нет сериализации уровня, непустую стартовую сцену нельзя молча игнорировать.
    if (!json["startup_scene"].IsNull()) return Fail(error, "Loading saved scenes is not implemented yet; startup_scene must be null.");
    Project candidate;
    candidate.name.assign(json["name"].GetString(), json["name"].GetStringLength());
    candidate.resourceDirectory.assign(json["resource_directory"].GetString(), json["resource_directory"].GetStringLength());
    if (!ValidText(candidate.name) || !RelativeDirectory(candidate.resourceDirectory)) return Fail(error, "Invalid project name or relative resource directory.");
    candidate.filePath = Utf8(path);
    if (!fs::is_directory(Disk(candidate.ResourceRoot()), ec) || ec) return Fail(error, "Project resource directory is missing or inaccessible.");
    result = std::move(candidate);
    return true;
}

bool Project::Create(const std::string& parent, const std::string& name, Project& result, std::string& error, const Project* source)
{
    error.clear();
    if (!ValidText(parent) || !ValidText(name) || name == "." || name == ".."
        || name.find_first_of("/\\:<>\"|?*") != std::string::npos || name.back() == '.' || name.back() == ' '
        || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) return Fail(error, "Choose a location and a valid folder name.");
    std::error_code ec;
    fs::path base = fs::absolute(Disk(parent), ec);
    if (ec || !fs::is_directory(base, ec) || ec) return Fail(error, "Parent directory is missing or inaccessible.");
    base = fs::weakly_canonical(base, ec);
    if (ec) return Fail(error, "Cannot resolve parent directory: " + ec.message());
    const fs::path root = base / Disk(name);
    if (source) {
        const fs::path sourceRoot = fs::weakly_canonical(Disk(source->ResourceRoot()), ec);
        if (ec) return Fail(error, "Cannot resolve source resource directory.");
        const DWORD attributes = GetFileAttributesW(Disk(source->ResourceRoot()).c_str());
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
        const fs::path sourceRoot = Disk(source->ResourceRoot());
        const fs::path destinationRoot = root / Disk(resourceDirectory);
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
                error = "Cannot copy resources: linked or inaccessible entry: " + Utf8(file);
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
    const fs::path manifest = root / (Disk(name).native() + L".ggproject");
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
    result.filePath = Utf8(manifest.lexically_normal());
    result.resourceDirectory = resourceDirectory;
    return true;
}

bool Project::CreateTemporary(Project& result, std::string& error)
{
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!required) return Fail(error, "LOCALAPPDATA is unavailable.");
    std::wstring directory(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(), required);
    if (!length || length >= required) return Fail(error, "Cannot read LOCALAPPDATA.");
    directory.resize(length);
    const fs::path drafts = fs::path(directory) / L"GGEngine/Editor/Drafts";
    std::error_code ec;
    fs::create_directories(drafts, ec);
    if (ec) return Fail(error, "Cannot create temporary project directory: " + ec.message());
    // Каждому запуску — отдельная папка, чтобы не затереть ресурсы другого редактора.
    const std::string name = "Unsaved-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    return Create(Utf8(drafts), name, result, error);
}

bool ProjectHistory::Load(std::string& error)
{
    error.clear();
    entries.clear();
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!count) return Fail(error, "LOCALAPPDATA is unavailable; recent projects cannot be saved.");
    std::wstring directory(count, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(), count);
    if (!length || length >= count) return Fail(error, "Cannot read LOCALAPPDATA.");
    directory.resize(length);
    const fs::path settings = fs::path(directory) / L"GGEngine/Editor/recent-projects.json";
    settingsFile = Utf8(settings);
    std::error_code ec;
    if (!fs::exists(settings, ec) && !ec) return true;
    rapidjson::Document json;
    if (!ReadJson(settings, json, error)) return false;
    if (!json.HasMember("recent") || !json["recent"].IsArray()) return Fail(error, "Invalid recent projects list.");
    for (const auto& item : json["recent"].GetArray()) {
        if (!item.IsString()) continue;
        const std::string file(item.GetString(), item.GetStringLength());
        if (ValidText(file) && Disk(file).is_absolute() && entries.size() < 15
            && std::find(entries.begin(), entries.end(), file) == entries.end()) entries.push_back(file);
    }
    return true;
}

bool ProjectHistory::Remember(const std::string& file, std::string& error)
{
    error.clear();
    if (settingsFile.empty()) return Fail(error, "Recent projects settings are unavailable.");
    // Windows сравнивает пути без учёта регистра; порядок — от последнего открытого.
    const auto wide = Disk(file).native();
    std::erase_if(entries, [&](const std::string& entry) { return _wcsicmp(Disk(entry).c_str(), wide.c_str()) == 0; });
    entries.insert(entries.begin(), file);
    if (entries.size() > 15) entries.resize(15);
    std::error_code ec;
    const fs::path settings = Disk(settingsFile);
    fs::create_directories(settings.parent_path(), ec);
    if (ec) return Fail(error, "Cannot create editor settings directory: " + ec.message());
    rapidjson::StringBuffer bytes;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(bytes);
    writer.StartObject(); writer.Key("recent"); writer.StartArray();
    for (const auto& entry : entries) writer.String(entry.data(), static_cast<rapidjson::SizeType>(entry.size()));
    writer.EndArray(); writer.EndObject();
    return WriteJson(settings, bytes, true, error);
}
