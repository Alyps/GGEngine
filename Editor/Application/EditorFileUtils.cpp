#include "EditorFileUtils.h"
#include "ResourceManager.h"
#include "Object.h"
#include "MeshComponent.h"
#include <atomic>
#include <algorithm>
#include <rapidjson/prettywriter.h>
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

    bool PickModelImportPath(HWND owner, bool executable, std::string& path, std::string& error, bool material, bool texture)
    {
        error.clear();
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return Fail(error, "Cannot initialize file dialog.");
        IFileOpenDialog* dialog = nullptr;
        HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(status)) {
            const COMDLG_FILTERSPEC filter = executable ? COMDLG_FILTERSPEC{ L"Blender executable", L"blender.exe" }
                : texture ? COMDLG_FILTERSPEC{ L"Texture images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.dds;*.tga" }
                : material ? COMDLG_FILTERSPEC{ L"Material resource", L"*.material.json" }
                : COMDLG_FILTERSPEC{ L"Static models", L"*.blend;*.glb;*.fbx" };
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


    namespace {
        std::string AssetName(std::string value)
        {
            // Префиксы файлов добавляются отдельно; имя пользователя проверяется перед Publish.
            for (char& c : value) if (static_cast<unsigned char>(c) < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
            while (!value.empty() && (value.back() == ' ' || value.back() == '.')) value.pop_back();
            if (value.empty()) value = "Model";
            return value.substr(0, 100);
        }

        bool Inside(const fs::path& directory, const fs::path& root)
        {
            const auto relative = directory.lexically_relative(root);
            if (relative.empty() || relative.is_absolute()) return false;
            for (const auto& part : relative) if (part == "..") return false;
            return true;
        }
    }

    struct ModelImport::Job {
        HANDLE thread = nullptr;
        std::atomic<bool> cancel{ false };
        std::atomic<int> stage{ 0 };
        bool publishing = false, ready = false, success = false, createObject = true;
        std::string source, blender, name, error, result;
        fs::path folder, root, temporary, destination;
        ImportedModel model;
        int replacementIndex = -1;
        std::string replacementPath;
        // Только эти созданные заданием файлы и каталоги удаляются при отмене.
        std::vector<fs::path> files, directories;
    };

    std::string ModelImport::ResourceFileName(bool material, size_t index, const std::string& name)
    {
        return std::string(material ? "Material_" : "Mesh_") + std::to_string(index) + "_" +
            AssetName(name) + (material ? ".material.json" : ".mesh");
    }

    ModelImport::~ModelImport() { Clean(); }
    ModelImport::ModelImport(ModelImport&& other) noexcept : job(std::exchange(other.job, nullptr)) {}
    ModelImport& ModelImport::operator=(ModelImport&& other) noexcept
    {
        if (this != &other) { Clean(); job = std::exchange(other.job, nullptr); }
        return *this;
    }
    bool ModelImport::Busy() const { return job && job->thread; }
    bool ModelImport::Ready() const { return job && !job->thread && job->ready; }
    ImportedModel* ModelImport::Data() { return Ready() ? &job->model : nullptr; }
    const char* ModelImport::Stage() const
    {
        if (!job) return "";
        switch (job->stage.load()) {
        case 7: return "Reading replacement texture...";
        case 1: return "Converting Blender scene...";
        case 2: return "Reading geometry, materials and textures...";
        case 3: return "Writing mesh resources...";
        case 4: return "Writing textures and materials...";
        case 5: return "Creating object...";
        case 6: return "Publishing imported resources...";
        default: return "Preparing import...";
        }
    }
    void ModelImport::Cancel() { if (job) job->cancel = true; }
    void ModelImport::Reset() { Clean(); }
    void ModelImport::Clean()
    {
        if (!job) return;
        job->cancel = true;
        // Поток обращается к Job до завершения; закрытие проекта сначала дожидается отмены.
        if (job->thread) { WaitForSingleObject(job->thread, INFINITE); CloseHandle(job->thread); }
        for (const auto& file : job->files) DeleteFileW(file.c_str());
        for (auto it = job->directories.rbegin(); it != job->directories.rend(); ++it) RemoveDirectoryW(it->c_str());
        delete job;
        job = nullptr;
    }

    bool ModelImport::Start(const std::string& source, const std::string& folder,
        const std::string& blender, const std::string& resourceRoot, std::string& error)
    {
        error.clear();
        if (Busy()) return Fail(error, "An import is already running.");
        Clean();
        std::error_code ec;
        const auto input = fs::canonical(DiskPath(source), ec);
        if (ec || !fs::is_regular_file(input, ec)) return Fail(error, "Choose an existing model file.");
        std::wstring extension = input.extension().wstring();
        for (auto& c : extension) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
        if (extension != L".blend" && extension != L".glb" && extension != L".fbx")
            return Fail(error, "Supported formats: .glb, .fbx, .blend.");
        const auto root = fs::canonical(DiskPath(resourceRoot), ec);
        if (ec) return Fail(error, "Resource root is unavailable.");
        const auto destination = fs::canonical(DiskPath(folder), ec);
        if (ec || !fs::is_directory(destination, ec) || !Inside(destination, root))
            return Fail(error, "Choose a destination inside the project's resource folder.");
        job = new (std::nothrow) Job();
        if (!job) return Fail(error, "Cannot allocate import job.");
        job->source = Utf8Path(input); job->blender = blender;
        job->folder = destination; job->root = root;
        // Собственный каталог создаётся рядом с результатом, чтобы публикация была переименованием.
        static unsigned counter = 0;
        job->temporary = destination / (L".ggimport-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter));
        if (!CreateDirectoryW(job->temporary.c_str(), nullptr)) { Clean(); return Fail(error, "Cannot create import staging directory."); }
        job->directories.push_back(job->temporary);
        job->thread = CreateThread(nullptr, 0, Run, job, 0, nullptr);
        if (!job->thread) { Clean(); return Fail(error, "Cannot start import worker."); }
        return true;
    }

    bool ModelImport::ReplaceTexture(size_t index, const std::string& path, std::string& error)
    {
        error.clear();
        if (!Ready() || index >= job->model.textures.size()) return Fail(error, "Analyze the model first.");
        job->replacementIndex = static_cast<int>(index); job->replacementPath = path;
        job->cancel = false; job->success = false; job->error.clear(); job->stage = 7;
        job->thread = CreateThread(nullptr, 0, Run, job, 0, nullptr);
        if (!job->thread) { job->replacementIndex = -1; return Fail(error, "Cannot start texture replacement."); }
        return true;
    }

    bool ModelImport::Publish(const std::string& name, bool createObject, std::string& error)
    {
        error.clear();
        if (!Ready()) return Fail(error, "Analyze the model first.");
        if (name.empty() || name != AssetName(name) || name == "." || name == "..")
            return Fail(error, "Use a valid folder name (up to 100 bytes, without path separators).");
        std::string deviceName = name.substr(0, name.find('.'));
        for (char& c : deviceName) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (deviceName == "CON" || deviceName == "PRN" || deviceName == "AUX" || deviceName == "NUL" ||
            (deviceName.size() == 4 && (deviceName.starts_with("COM") || deviceName.starts_with("LPT")) &&
             deviceName[3] >= '1' && deviceName[3] <= '9')) return Fail(error, "This name is reserved by Windows.");
        std::error_code ec;
        job->destination = job->folder / DiskPath(name);
        if (fs::exists(job->destination, ec) || ec) return Fail(error, "Destination already exists. Choose a new name.");
        job->name = name; job->createObject = createObject;
        job->publishing = true; job->success = false; job->error.clear();
        job->thread = CreateThread(nullptr, 0, Run, job, 0, nullptr);
        if (!job->thread) return Fail(error, "Cannot start export worker.");
        return true;
    }

    DWORD WINAPI ModelImport::Run(void* context)
    {
        auto& task = *static_cast<Job*>(context);
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const auto perform = [&]() -> bool {
            if (FAILED(apartment)) return Fail(task.error, "Cannot initialize image codecs.");
            if (task.replacementIndex >= 0)
                return task.model.ReplaceTexture(task.replacementIndex, task.replacementPath, task.cancel, task.error);
            if (!task.publishing) {
                std::string source = task.source;
                auto extension = DiskPath(source).extension().wstring();
                for (auto& c : extension) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
                if (extension == L".blend") {
                    task.stage = 1;
                    const auto output = task.temporary / L"model.glb";
                    task.files.push_back(output);
                    // Скрипт фиксирован; пути передаются отдельными argv, автозапуск исходника отключён.
                    const std::wstring script = L"import bpy,sys; bpy.ops.export_scene.gltf(filepath=sys.argv[-1], export_format='GLB', "
                        L"export_animations=False, export_skins=False, export_morph=False, export_materials='EXPORT', "
                        L"export_apply=True, use_active_scene=True)";
                    std::wstring executable = DiskPath(task.blender).wstring();
                    if (!DiskPath(task.blender).has_parent_path()) {
                        wchar_t found[32768]{};
                        const DWORD length = SearchPathW(nullptr, executable.c_str(), L".exe", 32768, found, nullptr);
                        if (!length || length >= 32768) return Fail(task.error, "Blender was not found. Select blender.exe.");
                        executable = found;
                    }
                    std::wstring command = QuoteArgument(executable) + L" --background --factory-startup --disable-autoexec " +
                        QuoteArgument(DiskPath(source).wstring()) + L" --python-exit-code 1 --python-expr " +
                        QuoteArgument(script) + L" -- " + QuoteArgument(output.wstring());
                    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
                    PROCESS_INFORMATION child{};
                    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, DiskPath(source).parent_path().c_str(), &startup, &child))
                        return Fail(task.error, "Cannot launch Blender.");
                    CloseHandle(child.hThread);
                    while (WaitForSingleObject(child.hProcess, 100) == WAIT_TIMEOUT) {
                        if (task.cancel) TerminateProcess(child.hProcess, ERROR_CANCELLED);
                    }
                    DWORD code = 1;
                    GetExitCodeProcess(child.hProcess, &code); CloseHandle(child.hProcess);
                    if (task.cancel) return Fail(task.error, "Import cancelled.");
                    if (code) return Fail(task.error, "Blender export failed (exit code " + std::to_string(code) + ").");
                    source = Utf8Path(output);
                }
                task.stage = 2;
                if (!task.model.Read(source, task.cancel, task.error)) return false;
                if (extension == L".blend") task.model.warnings.push_back("Blender exports the active scene as static glTF. Procedural materials may require baking.");
                return !task.cancel;
            }
            const auto output = task.temporary / L"output";
            for (const auto& directory : { output, output / L"Meshes", output / L"Materials", output / L"Textures" }) {
                if (!CreateDirectoryW(directory.c_str(), nullptr)) return Fail(task.error, "Cannot create resource output directory.");
                task.directories.push_back(directory);
            }
            ResourceManager references;
            references.SetResourceDirectory(Utf8Path(task.root));
            const auto reference = [&](const fs::path& relative) { return references.ResourcePath(Utf8Path(task.destination / relative)); };
            std::vector<std::string> meshPaths, materialPaths;
            task.stage = 3;
            for (size_t i = 0; i < task.model.meshes.size(); ++i) {
                if (task.cancel) return Fail(task.error, "Import cancelled.");
                const auto& mesh = task.model.meshes[i];
                const auto relative = fs::path("Meshes") / DiskPath(ResourceFileName(false, i, mesh.name));
                task.files.push_back(output / relative);
                if (!MeshDataResource::Save(Utf8Path(output / relative), mesh.data, task.error)) return false;
                meshPaths.push_back(reference(relative));
            }
            task.stage = 4;
            std::vector<bool> usedTextures(task.model.textures.size(), false);
            for (const auto& material : task.model.materials) if (material.existingPath.empty()) {
                for (int index : { material.diffuse, material.normal, material.ao }) if (index >= 0) usedTextures[index] = true;
            }
            for (size_t i = 0; i < task.model.textures.size(); ++i) {
                if (!usedTextures[i] || task.model.textures[i].png.empty()) continue;
                const auto& texture = task.model.textures[i];
                if (task.cancel) return Fail(task.error, "Import cancelled.");
                const auto file = output / L"Textures" / DiskPath(texture.name);
                task.files.push_back(file);
                std::ofstream stream(file, std::ios::binary);
                stream.write(reinterpret_cast<const char*>(texture.png.data()), static_cast<std::streamsize>(texture.png.size()));
                stream.close();
                if (!stream) return Fail(task.error, "Cannot write texture resource.");
            }
            for (size_t i = 0; i < task.model.materials.size(); ++i) {
                if (task.cancel) return Fail(task.error, "Import cancelled.");
                const auto& material = task.model.materials[i];
                if (!material.existingPath.empty()) {
                    std::error_code ec;
                    const auto file = fs::canonical(DiskPath(material.existingPath), ec);
                    MaterialData checked;
                    if (ec || !Inside(file, task.root) || !checked.Load(Utf8Path(file), task.error))
                        return Fail(task.error, "Existing material must be a valid resource inside the project: " + material.existingPath);
                    materialPaths.push_back(references.ResourcePath(Utf8Path(file)));
                    continue;
                }
                auto data = material.data;
                const auto texture = [&](int index) {
                    return index < 0 || task.model.textures[index].png.empty() ? std::string{} : reference(fs::path("Textures") / DiskPath(task.model.textures[index].name));
                };
                data.diffuse = texture(material.diffuse); data.normal = texture(material.normal); data.ao = texture(material.ao);
                const auto relative = fs::path("Materials") / DiskPath(ResourceFileName(true, i, material.name));
                task.files.push_back(output / relative);
                if (!data.Save(Utf8Path(output / relative), task.error)) return false;
                materialPaths.push_back(reference(relative));
            }
            if (task.createObject) {
                task.stage = 5;
                GameObject object;
                object.getTransform().set_name(task.name);
                std::vector<TransformNode*> nodes;
                for (size_t i = 0; i < task.model.nodes.size(); ++i) {
                    if (task.cancel) return Fail(task.error, "Import cancelled.");
                    const auto& source = task.model.nodes[i];
                    auto* parent = source.parent < 0 ? &object.getTransform() : nodes[source.parent];
                    std::string name = source.name;
                    if (parent->find_child(name)) name += "_" + std::to_string(i);
                    while (parent->find_child(name)) name += "_";
                    auto* node = parent->add_child(name, source.position, source.rotation, source.scale);
                    if (!node) return Fail(task.error, "Cannot allocate imported transform node.");
                    nodes.push_back(node);
                    for (unsigned index : source.meshes) {
                        if (!node->create_component<MeshComponent>(meshPaths[index], materialPaths[task.model.meshes[index].material]))
                            return Fail(task.error, "Cannot allocate imported mesh component.");
                    }
                }
                const auto file = output / DiskPath(task.name + ".object.json");
                task.files.push_back(file);
                if (!object.Save(Utf8Path(file), task.error)) return false;
            }
            if (task.cancel) return Fail(task.error, "Import cancelled.");
            task.stage = 6;
            // Проверяем неизменность физической папки перед публикацией и не заменяем чужой результат.
            std::error_code ec;
            if (fs::canonical(task.folder, ec) != task.folder || ec || !Inside(task.folder, task.root))
                return Fail(task.error, "Destination folder changed during import.");
            if (!MoveFileExW(output.c_str(), task.destination.c_str(), MOVEFILE_WRITE_THROUGH))
                return Fail(task.error, "Cannot publish resources; destination may already exist.");
            task.result = Utf8Path(task.destination);
            return true;
        };
        task.success = perform();
        if (task.cancel && !task.success && task.error.empty()) task.error = "Import cancelled.";
        if (SUCCEEDED(apartment)) CoUninitialize();
        return 0;
    }

    bool ModelImport::Poll(std::string& result, std::string& error)
    {
        if (!Busy() || WaitForSingleObject(job->thread, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(job->thread); job->thread = nullptr;
        result.clear(); error = job->error;
        // Неудачная замена не уничтожает результаты анализа и остальные назначения.
        if (job->replacementIndex >= 0) {
            job->replacementIndex = -1; job->cancel = false;
            return true;
        }
        if (job->success && !job->publishing) { job->ready = true; return true; }
        if (job->success) result = job->result;
        Clean();
        return true;
    }
}
