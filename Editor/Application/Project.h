#pragma once
#include <string>
#include <vector>
class Level;
class EditorCamera;

// Описание проекта принадлежит редактору; движок получает только файловый корень.
struct Project {
    std::string name;
    std::string filePath;
    std::string resourceDirectory = "res";
    std::string startupScene;
    bool Save(std::string& error) const;
    std::string Root() const;
    std::string ResourceRoot() const;
    static bool Open(const std::string& file, Project& result, std::string& error);
    static bool Create(const std::string& parent, const std::string& name,
        Project& result, std::string& error, const Project* source = nullptr);
    static bool CreateTemporary(Project& result, std::string& error);
    // Level владеет камерой и демонстрацией; возвращаемый адрес заимствован, Init не вызывается.
    static EditorCamera* CreateSceneCamera(Level& level);
    static bool PopulateDemoScene(Level& level);
};

// В пользовательских настройках хранятся только пути, а не содержимое проектов.
class ProjectHistory {
public:
    bool Load(std::string& error);
    bool Remember(const std::string& file, std::string& error);
    const std::vector<std::string>& Entries() const { return entries; }
private:
    std::string settingsFile;
    std::vector<std::string> entries;
};
