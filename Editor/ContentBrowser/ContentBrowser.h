#pragma once
#include <string>
#include <vector>

// Editor владеет панелью; записи описывают файлы и не загружают ресурсы.
class ContentBrowser {
public:
    explicit ContentBrowser(const std::string& rootPath, const std::string& projectPath = {});
    void Draw(bool& open);
    bool IsFocused() const { return focused; }

private:
    struct Entry {
        std::string name;
        bool directory = false;
        bool link = false;
        bool mesh = false;
    };
    void Refresh();
    bool CheckDirectory();
    enum class CreateType { Folder, Material, LuaScript };
    void CreateEntry();
    void DrawCreateDialog();
    void DrawDeleteDialog();

    std::string root;
    std::string projectRoot;
    std::string current;
    std::string selected;
    std::string error;
    std::string actionError;
    std::string pendingDelete;
    std::vector<Entry> entries;
    char entryName[256]{};
    CreateType createType = CreateType::Folder;
    bool focused = false;
    bool wasOpen = false;
    bool available = false;
    bool rootMissing = false;
};
