#pragma once
#include <filesystem>
#include <string>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <windows.h>

// Файловые операции редактора: обычные UTF-8 пути и атомарная запись JSON.
namespace EditorFiles {
    std::filesystem::path DiskPath(const std::string& text);
    std::string Utf8Path(const std::filesystem::path& path);
    bool UserDirectory(std::filesystem::path& path, std::string& error);
    bool ReadJson(const std::filesystem::path& path, rapidjson::Document& json, std::string& error);
    bool WriteJson(const std::filesystem::path& path, const rapidjson::StringBuffer& bytes, bool replace, std::string& error);
    bool PickObjectSavePath(HWND owner, std::string& result, std::string& error);
    bool PickProjectPath(HWND owner, bool folder, std::string& path, std::string& error);
    bool PickModelImportPath(HWND owner, bool executable, std::string& path, std::string& error);

    // Панель владеет заданием; перемещение передаёт процесс, уничтожение отменяет его.
    class ModelImport {
    public:
        ModelImport() = default;
        ~ModelImport();
        ModelImport(const ModelImport&) = delete;
        ModelImport& operator=(const ModelImport&) = delete;
        ModelImport(ModelImport&& other) noexcept;
        ModelImport& operator=(ModelImport&& other) noexcept;
        bool Start(const std::string& source, const std::string& folder,
            const std::string& blender, std::string& error);
        bool Poll(std::string& result, std::string& error);
        bool Busy() const { return !temporary.empty(); }
        void Cancel();
    private:
        void Clean();
        HANDLE process = nullptr;
        std::filesystem::path temporary, destination;
        bool cancelled = false;
    };
}
