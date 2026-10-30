
#include <core.h>
#include <iostream>

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
    std::wstring projectDirectory, scenePath, standardDirectory;
    // Каждый параметр принимает ровно одно значение; опечатки не игнорируются.
    for (int i = 1; i < argc; i += 2) {
        const std::wstring option = argv[i];
        std::wstring* destination = nullptr;
        if (option == L"--project") destination = &projectDirectory;
        else if (option == L"--scene") destination = &scenePath;
        else if (option == L"--standard-resources") destination = &standardDirectory;
        if (!destination || i + 1 >= argc || !destination->empty() || !*argv[i + 1]) {
            std::cerr << "Invalid, duplicate or incomplete launch argument\n";
            return 1;
        }
        *destination = argv[i + 1];
    }
    if (projectDirectory.empty() || scenePath.empty()) {
        std::cerr << "Usage: Game.exe --project <directory> --scene <level.json> "
            "[--standard-resources <directory>]\n";
        return 1;
    }

    // Только дочерний процесс меняет cwd: относительные ссылки считаются от проекта.
    if (!SetCurrentDirectoryW(projectDirectory.c_str())) {
        std::cerr << "Cannot open project directory (Win32 " << GetLastError() << ")\n";
        return 1;
    }
    std::string scene, standard;
    if (!ToUtf8(scenePath, scene) || (!standardDirectory.empty() && !ToUtf8(standardDirectory, standard))) {
        std::cerr << "Invalid Unicode launch path\n";
        return 1;
    }

    // Game на стеке владеет загруженным уровнем и освобождает его раньше систем.
    Game game(L"GGEngine Game");
    if (!game.getWindow().getWindow() || !game.getRenderServer().Device()) {
        std::cerr << "Cannot initialize game window or renderer\n";
        return 1;
    }
    if (!standard.empty()) game.getResourceManager().SetStandardResourceDirectory(standard);
    std::string error;
    if (!game.LoadLevelFromFile(scene, error)) {
        std::cerr << "Cannot load start level: " << error << '\n';
        return 1;
    }
    game.Init();

    // Общий индекс объекта включает компоненты всех веток TransformTree.
    EditorCamera* camera = nullptr;
    for (GameObject* object : game.getLevel()->getObjects()) {
        if (object->isPendingDestroy()) continue;
        for (Component* component : object->getComponents()) {
            auto* candidate = dynamic_cast<EditorCamera*>(component);
            if (candidate && !candidate->isPendingDestroy() && candidate->getTransformNode() &&
                !candidate->getTransformNode()->isPendingDestroy()) {
                camera = candidate;
                break;
            }
        }
        if (camera) break;
    }
    if (!camera) {
        std::cerr << "Start level has no camera attached to a transform\n";
        return 1;
    }
    game.getRenderServer().setCamera(camera);
    const int result = game.Run();
    if (result && !game.getPhysicsServer().getLastError().empty())
        std::cerr << "Physics failed: " << game.getPhysicsServer().getLastError() << '\n';
    return result;
}
