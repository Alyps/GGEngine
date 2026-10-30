#include "EditorFileUtils.h"
#include <fstream>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace fs = std::filesystem;
namespace { bool Fail(std::string& error, const std::string& text) { error = text; return false; } }
namespace EditorFiles {
    fs::path DiskPath(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }
    std::string Utf8Path(const fs::path& path) {
        const auto text = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
    bool UserDirectory(fs::path& path, std::string& error) {
        const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (!count) return Fail(error, "LOCALAPPDATA is unavailable.");
        std::wstring directory(count, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(), count);
        if (!length || length >= count) return Fail(error, "Cannot read LOCALAPPDATA.");
        directory.resize(length);
        path = fs::path(directory) / L"GGEngine/Editor";
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
    bool PickObjectSavePath(HWND owner, std::string& result, std::string& error)
    {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) { error = "Cannot initialize save dialog."; return false; }
        IFileSaveDialog* dialog = nullptr;
        HRESULT status = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(status)) {
            const COMDLG_FILTERSPEC filter{ L"GGEngine object", L"*.object.json" };
            status = dialog->SetFileTypes(1, &filter);
            if (SUCCEEDED(status)) status = dialog->SetDefaultExtension(L"object.json");
            if (SUCCEEDED(status)) status = dialog->SetFileName(L"Object.object.json");
            if (SUCCEEDED(status)) status = dialog->Show(owner);
            IShellItem* item = nullptr;
            if (SUCCEEDED(status)) status = dialog->GetResult(&item);
            if (item) {
                PWSTR path = nullptr;
                status = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(status)) {
                    const auto text = std::filesystem::path(path).generic_u8string();
                    result.assign(reinterpret_cast<const char*>(text.data()), text.size());
                }
                CoTaskMemFree(path); item->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
        if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_CANCELLED)) error = "Cannot open object save dialog.";
        if (SUCCEEDED(status)) {
            std::string lower = result;
            for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (!lower.ends_with(".object.json")) { error = "Choose a file name ending in .object.json."; return false; }
        }
        return SUCCEEDED(status);
    }
    bool PickProjectPath(HWND owner, bool folder, std::string& path, std::string& error)
    {
        // Диалог владеет COM-ссылками только до возвращения; отмена не является ошибкой.
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) { error = "Cannot initialize file dialog."; return false; }
        IFileOpenDialog* dialog = nullptr;
        HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(status)) {
            DWORD options = 0;
            status = dialog->GetOptions(&options);
            if (SUCCEEDED(status)) status = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
            const COMDLG_FILTERSPEC filter{ L"GGEngine project", L"*.ggproject" };
            if (SUCCEEDED(status) && !folder) status = dialog->SetFileTypes(1, &filter);
            if (SUCCEEDED(status)) status = dialog->Show(owner);
            IShellItem* item = nullptr;
            if (SUCCEEDED(status)) status = dialog->GetResult(&item);
            if (item) {
                PWSTR text = nullptr;
                status = item->GetDisplayName(SIGDN_FILESYSPATH, &text);
                if (SUCCEEDED(status)) {
                    const auto utf8 = std::filesystem::path(text).generic_u8string();
                    path.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
                }
                CoTaskMemFree(text);
                item->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
        if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_CANCELLED)) error = "Cannot open file dialog (HRESULT " + std::to_string(status) + ").";
        return SUCCEEDED(status);
    }
}
