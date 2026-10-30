#pragma once
#include <string>
#include <vector>

// Editor владеет панелью; записи описывают файлы и не загружают ресурсы.
class ContentBrowser {
public:
    explicit ContentBrowser(const std::string& rootPath, const std::string& projectPath = {});
    void Draw(bool& open);
    bool IsFocused() const { return focused; }
    std::string TakeObjectToOpen() { std::string result; result.swap(objectToOpen); return result; }
    std::string TakeSceneToOpen() { std::string result; result.swap(sceneToOpen); return result; }
    void RefreshFiles() { if (!root.empty()) Refresh(); }

private:
    // Запросы текущего кадра применяются после обхода списка, чтобы не инвалидировать entries.
    struct DrawRequests {
        bool refresh = false, create = false, remove = false;
        std::string directory;
    };
    void DrawToolbar(DrawRequests& requests);
    size_t DrawFileList(float footerHeight, DrawRequests& requests);
    void ApplyFileActions(DrawRequests& requests);
    void DrawCreateMenu(DrawRequests& requests);
    struct Entry {
        std::string name;
        bool directory = false;
        bool link = false;
        bool mesh = false;
        unsigned long long bytes = 0;
        bool sizeKnown = false;
    };
    void Refresh();
    bool CheckDirectory();
    bool Navigate(const std::string& path, bool remember = true);
    void Activate(const Entry& entry, std::string& directory);
    void OpenFolder(const std::string& path);
    std::string ResourcePath(const std::string& path) const;
    enum class CreateType { Folder, Material, LuaScript };
    void CreateEntry();
    void DrawCreateDialog();
    void DrawDeleteDialog();

    std::string root;
    std::string projectRoot;
    std::string current;
    std::string selected;
    std::string objectToOpen;
    std::string sceneToOpen;
    std::vector<std::string> navigation;
    size_t navigationPosition = 0;
    char search[256]{};
    int typeFilter = 0;
    bool tiles = false;
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
