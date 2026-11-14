#include "EditorFileUtils.h"
#include "ResourceManager.h"
#include <fstream>
#include <utility>
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

    bool PickModelImportPath(HWND owner, bool executable, std::string& path, std::string& error)
    {
        error.clear();
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return Fail(error, "Cannot initialize file dialog.");
        IFileOpenDialog* dialog = nullptr;
        HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(status)) {
            const COMDLG_FILTERSPEC filter = executable ? COMDLG_FILTERSPEC{ L"Blender executable", L"blender.exe" }
                : COMDLG_FILTERSPEC{ L"Static models", L"*.blend;*.glb" };
            status = dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
            if (SUCCEEDED(status)) status = dialog->SetFileTypes(1, &filter);
            if (SUCCEEDED(status)) status = dialog->Show(owner);
            IShellItem* item = nullptr;
            if (SUCCEEDED(status)) status = dialog->GetResult(&item);
            if (item) {
                // COM владеет строкой до CoTaskMemFree; наружу возвращается UTF-8 копия.
                PWSTR text = nullptr;
                status = item->GetDisplayName(SIGDN_FILESYSPATH, &text);
                if (SUCCEEDED(status)) path = Utf8Path(fs::path(text));
                CoTaskMemFree(text); item->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
        if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_CANCELLED)) return Fail(error, "Cannot open model import dialog.");
        return SUCCEEDED(status);
    }

    namespace {
        // Правила argv Windows: обратные слеши перед кавычкой и в конце удваиваются.
        std::wstring QuoteArgument(const std::wstring& value)
        {
            std::wstring result = L"\"";
            size_t slashes = 0;
            for (wchar_t c : value) {
                if (c == L'\\') { ++slashes; continue; }
                result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                result += c; slashes = 0;
            }
            result.append(slashes * 2, L'\\');
            return result + L"\"";
        }
    }

    ModelImport::~ModelImport() { Clean(); }
    ModelImport::ModelImport(ModelImport&& other) noexcept { *this = std::move(other); }
    ModelImport& ModelImport::operator=(ModelImport&& other) noexcept
    {
        if (this == &other) return *this;
        Clean();
        process = std::exchange(other.process, nullptr);
        temporary.swap(other.temporary); destination.swap(other.destination);
        cancelled = std::exchange(other.cancelled, false);
        return *this;
    }

    void ModelImport::Cancel()
    {
        cancelled = true;
        if (process) TerminateProcess(process, ERROR_CANCELLED);
    }

    void ModelImport::Clean()
    {
        bool stopped = true;
        if (process) {
            // При закрытии проекта не оставляем работающий Blender с путями старого проекта.
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) TerminateProcess(process, ERROR_CANCELLED);
            stopped = WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
            CloseHandle(process); process = nullptr;
        }
        if (stopped && !temporary.empty()) {
            // Удаляем только известный результат собственного задания, без рекурсивного удаления.
            DeleteFileW((temporary / L"model.glb").c_str());
            RemoveDirectoryW(temporary.c_str());
        }
        temporary.clear(); destination.clear(); cancelled = false;
    }

    bool ModelImport::Start(const std::string& source, const std::string& folder,
        const std::string& blender, std::string& error)
    {
        error.clear();
        if (Busy()) return Fail(error, "A model import is already running.");
        std::error_code ec;
        const fs::path input = fs::absolute(DiskPath(source), ec);
        if (ec || !fs::is_regular_file(input, ec)) return Fail(error, "Choose an existing .blend or .glb file.");
        std::wstring extension = input.extension().wstring();
        for (wchar_t& c : extension) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
        if (extension != L".blend" && extension != L".glb") return Fail(error, "Only .blend and .glb imports are supported.");
        const fs::path directory = fs::absolute(DiskPath(folder), ec);
        if (ec || !fs::is_directory(directory, ec)) return Fail(error, "The destination folder is unavailable.");
        destination = directory / input.stem(); destination += L".glb";
        if (fs::exists(destination, ec) || ec) return Fail(error, "A file with this name already exists or cannot be accessed. Choose another folder or rename the source.");

        static unsigned counter = 0;
        const fs::path staging = directory / (L".ggimport-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter));
        if (!CreateDirectoryW(staging.c_str(), nullptr)) return Fail(error, "Cannot create a temporary import folder.");
        temporary = staging; cancelled = false;
        const fs::path output = temporary / L"model.glb";
        if (extension == L".glb") {
            // Копия проверяется в новой папке: внешние буферы не должны зависеть от исходника.
            if (CopyFileW(input.c_str(), output.c_str(), TRUE)) return true;
            const DWORD code = GetLastError(); Clean();
            return Fail(error, "Cannot copy GLB file (Win32 " + std::to_string(code) + ").");
        }

        // Пути передаются отдельными argv после --, никогда не подставляются в Python-код.
        const std::wstring script = L"import bpy,sys; bpy.ops.export_scene.gltf(filepath=sys.argv[-1], export_format='GLB', "
            L"export_animations=False, export_skins=False, export_morph=False, export_materials='NONE', "
            L"export_apply=True, use_active_scene=True)";
        std::wstring executable = DiskPath(blender).wstring();
        if (executable.empty()) { Clean(); return Fail(error, "Choose blender.exe to import .blend files."); }
        // Разрешаем имя из PATH; выбранный абсолютный путь передаём CreateProcessW напрямую.
        if (!DiskPath(blender).has_parent_path()) {
            wchar_t found[32768]{};
            const DWORD count = SearchPathW(nullptr, executable.c_str(), L".exe", 32768, found, nullptr);
            if (!count || count >= 32768) { Clean(); return Fail(error, "Blender was not found. Select the installed blender.exe."); }
            executable = found;
        }
        std::wstring command = QuoteArgument(executable) + L" --background --factory-startup --disable-autoexec "
            + QuoteArgument(input.wstring()) + L" --python-exit-code 1 --python-expr " + QuoteArgument(script)
            + L" -- " + QuoteArgument(output.wstring());
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, input.parent_path().c_str(), &startup, &child)) {
            const DWORD code = GetLastError(); Clean();
            return Fail(error, "Cannot start Blender (Win32 " + std::to_string(code) + "). Check the executable path.");
        }
        CloseHandle(child.hThread); process = child.hProcess;
        return true;
    }

    bool ModelImport::Poll(std::string& result, std::string& error)
    {
        if (!Busy()) return false;
        if (process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return false;
        result.clear(); error.clear();
        DWORD code = 0;
        if (process && !GetExitCodeProcess(process, &code)) code = ERROR_GEN_FAILURE;
        if (cancelled) error = "Model import cancelled.";
        else if (code) error = "Blender failed (exit code " + std::to_string(code) + "). Check that this Blender version can open the file and export glTF.";
        else {
            const fs::path output = temporary / L"model.glb";
            if (!MeshResource::ValidateFile(Utf8Path(output), error)) error = "Cannot import model geometry: " + error;
            // Без REPLACE_EXISTING: даже появившийся во время конвертации файл не перезаписывается.
            else if (!MoveFileExW(output.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
                error = "Cannot publish imported GLB. The destination may already exist or be unavailable.";
            else result = Utf8Path(destination);
        }
        Clean();
        return true;
    }
}
