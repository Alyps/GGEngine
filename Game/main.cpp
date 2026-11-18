
#include <core.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

namespace {
    bool ToUtf8(const std::wstring& value, std::string& result)
    {
        // Аргументы Windows приходят в UTF-16, пути движка хранятся в UTF-8.
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (!size) return false;
        result.resize(size);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            value.c_str(), -1, result.data(), size, nullptr, nullptr)) return false;
        result.pop_back();
        return true;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    std::wstring projectDirectory, scenePath, standardDirectory, resourceDirectory;
    if (argc == 1) {
        // Экспорт запускается относительно EXE, даже если ярлык задаёт другую cwd.
        std::wstring module(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (!length || length >= module.size()) return 1;
        module.resize(length);
        const auto root = std::filesystem::path(module).parent_path();
        std::ifstream input(root / L"game.json", std::ios::binary);
        rapidjson::Document settings;
        if (input) {
            rapidjson::IStreamWrapper stream(input);
            settings.ParseStream(stream);
        }
        // Все пути манифеста локальны для экспортированной папки.
        const auto readPath = [&](const char* key, std::wstring& result) {
            if (!settings.IsObject() || !settings.HasMember(key) || !settings[key].IsString()) return false;
            const auto& value = settings[key];
            const std::string text(value.GetString(), value.GetStringLength());
            if (text.empty() || text.find('\0') != std::string::npos) return false;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (!count) return false;
            result.resize(count);
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count)) return false;
            const std::filesystem::path path(result);
            if (path.has_root_path() || result.find(L':') != std::wstring::npos) return false;
            for (const auto& part : path) if (part == L"..") return false;
            return true;
        };
        if (settings.HasParseError() || !readPath("startup_scene", scenePath)
            || !readPath("resource_directory", resourceDirectory)
            || !readPath("standard_resources", standardDirectory)) {
            MessageBoxW(nullptr, L"Cannot read game.json next to Game.exe.", L"GGEngine Game", MB_OK | MB_ICONERROR);
            return 1;
        }
        projectDirectory = root.native();
    }
    // Каждый параметр принимает ровно одно значение; опечатки не игнорируются.
    for (int i = 1; i < argc; i += 2) {
        const std::wstring option = argv[i];
        std::wstring* destination = nullptr;
        if (option == L"--project") destination = &projectDirectory;
        else if (option == L"--scene") destination = &scenePath;
        else if (option == L"--resources") destination = &resourceDirectory;
        else if (option == L"--standard-resources") destination = &standardDirectory;
        if (!destination || i + 1 >= argc || !destination->empty() || !*argv[i + 1]) {
            std::cerr << "Invalid, duplicate or incomplete launch argument\n";
            return 1;
        }
        *destination = argv[i + 1];
    }
    if (projectDirectory.empty() || scenePath.empty()) {
        std::cerr << "Usage: Game.exe --project <directory> --scene <level.json> "
            "[--standard-resources <directory>] [--resources <directory>]\n";
        return 1;
    }

    // Только дочерний процесс меняет cwd: относительные ссылки считаются от проекта.
    if (!SetCurrentDirectoryW(projectDirectory.c_str())) {
        std::cerr << "Cannot open project directory (Win32 " << GetLastError() << ")\n";
        return 1;
    }
    std::string scene, standard, resources;
    if ((!resourceDirectory.empty() && !ToUtf8(resourceDirectory, resources)) || !ToUtf8(scenePath, scene) || (!standardDirectory.empty() && !ToUtf8(standardDirectory, standard))) {
        std::cerr << "Invalid Unicode launch path\n";
        return 1;
    }

    // Game на стеке владеет загруженным уровнем и освобождает его раньше систем.
    Game game(L"GGEngine Game");
    if (!game.getWindow().getWindow() || !game.getRenderServer().Device()) {
        std::cerr << "Cannot initialize game window or renderer\n";
        return 1;
    }
    if (!resources.empty()) game.getResourceManager().SetResourceDirectory(resources);
    if (!standard.empty()) game.getResourceManager().SetStandardResourceDirectory(standard);
    std::string error;
    if (!game.LoadLevelFromFile(scene, error)) {
        std::cerr << "Cannot load start level: " << error << '\n';
        return 1;
    }
    game.Init();

    // Общий индекс объекта включает компоненты всех веток TransformTree.
    GameCamera* camera = GameCamera::Find(*game.getLevel());
    if (!camera) {
        std::cerr << "Start level has no GameCamera attached to a transform\n";
        return 1;
    }
    game.getRenderServer().setCamera(camera);
    const int result = game.Run();
    if (result && !game.getPhysicsServer().getLastError().empty())
        std::cerr << "Physics failed: " << game.getPhysicsServer().getLastError() << '\n';
    return result;
}
