#include "ContentBrowser.h"
#include "imgui.h"
#include "ResourceManager.h"
#include <algorithm>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace {
    // Публичные пути остаются UTF-8 строками; Windows получает UTF-16.
    fs::path DiskPath(const std::string& value)
    {
        return fs::path(std::u8string(value.begin(), value.end()));
    }

    std::string Utf8(const fs::path& value)
    {
        const auto text = value.generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    std::string LowerAscii(std::string value)
    {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    }

    // Метка описывает расширение; распознавание не означает импорт в движок.
    const char* FileType(const std::string& name)
    {
        const std::string lower = LowerAscii(name);
        if (lower.ends_with(".material.json")) return "[Material]";
        const auto dot = lower.find_last_of('.');
        const std::string extension = dot == std::string::npos ? "" : lower.substr(dot);
        if (extension == ".lua") return "[Lua]";
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg"
            || extension == ".bmp" || extension == ".tga" || extension == ".dds" || extension == ".hdr") return "[Image]";
        if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb") return "[Model]";
        if (extension == ".json") return "[JSON]";
        return "[File]";
    }

    // CREATE_NEW исключает перезапись существующего файла даже при внешних изменениях.
    bool WriteNewFile(const fs::path& path, const std::string& contents, std::string& error)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = "Cannot create file (Win32 " + std::to_string(GetLastError()) + "). The name may already exist.";
            return false;
        }
        DWORD written = 0;
        const bool complete = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
            && written == contents.size();
        const bool closed = CloseHandle(file) != FALSE;
        if (!complete || !closed) {
            // Удаляем только созданный нами незавершённый файл.
            const bool removed = DeleteFileW(path.c_str()) != FALSE;
            error = removed ? "Cannot write file; incomplete file removed."
                : "Cannot write file; incomplete file could not be removed.";
            return false;
        }
        return true;
    }
}

ContentBrowser::ContentBrowser(const std::string& rootPath)
    : root(Utf8(DiskPath(rootPath).lexically_normal())), current(root) {}

bool ContentBrowser::CheckDirectory()
{
    // Не входим в junction/symlink, включая заменённую извне родительскую папку.
    for (fs::path path = DiskPath(current); !path.empty(); path = path.parent_path()) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            error = "Directory unavailable (Win32 " + std::to_string(GetLastError()) + "): " + Utf8(path);
            return false;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = "Expected a regular directory: " + Utf8(path);
            return false;
        }
        if (path == path.parent_path()) break;
    }
    return true;
}

void ContentBrowser::Refresh()
{
    entries.clear();
    selected.clear();
    error.clear();
    available = false;
    // Отсутствующий корень создаётся только отдельной кнопкой пользователя.
    std::error_code ec;
    rootMissing = !fs::exists(DiskPath(root), ec) && !ec;
    if (!CheckDirectory()) return;
    fs::directory_iterator iterator(DiskPath(current), ec), end;
    if (ec) { error = "Cannot read directory: " + ec.message(); return; }

    // Перечисление выполняется по запросу, а не в каждом кадре ImGui.
    while (iterator != end) {
        const auto& path = iterator->path();
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            error = "Directory changed or entry unavailable; refresh to retry.";
            entries.clear();
            return;
        }
        const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        // Узнаём форматы у той же библиотеки, которая будет загружать ресурс.
        entries.push_back({ Utf8(path.filename()), directory, link,
            !directory && !link && MeshResource::SupportsExtension(LowerAscii(Utf8(path.extension()))) });
        iterator.increment(ec);
        if (ec) { error = "Cannot read directory: " + ec.message(); entries.clear(); return; }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return a.name < b.name;
    });
    available = true;
}

void ContentBrowser::CreateEntry()
{
    std::string name(entryName);
    // Разрешаем только одно имя, без относительных переходов и Win32-суффиксов.
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' '
        || name.find_first_of("/\\:*?\"<>|") != std::string::npos
        || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) {
        actionError = "Enter a name without separators or reserved characters.";
        return;
    }
    // Win32 запрещает имена устройств также перед расширением (например CON.txt).
    std::string base = name.substr(0, name.find('.'));
    for (char& c : base) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
        || (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT"))
            && base[3] >= '1' && base[3] <= '9')) {
        actionError = "This name is reserved by Windows.";
        return;
    }
    if (!CheckDirectory()) { actionError = error; return; }
    // Достраиваем расширение; stone.json также становится stone.material.json.
    if (createType == CreateType::Material && !LowerAscii(name).ends_with(".material.json")) {
        if (LowerAscii(name).ends_with(".json")) name.resize(name.size() - 5);
        name += ".material.json";
    }
    if (createType == CreateType::LuaScript && !LowerAscii(name).ends_with(".lua")) name += ".lua";
    const fs::path destination = DiskPath(current) / DiskPath(name);
    if (createType == CreateType::Folder) {
        std::error_code ec;
        if (!fs::create_directory(destination, ec)) {
            actionError = ec ? "Cannot create folder: " + ec.message() : "A folder with this name already exists.";
            return;
        }
    } else {
        // Шаблон соответствует MaterialData версии 1 и не требует GPU для создания.
        const std::string contents = createType == CreateType::Material
            ? "{\n  \"version\": 1,\n  \"shader\": \"builtin/simple\",\n  \"render_state\": \"opaque\",\n"
              "  \"sampler\": \"wrap_linear\",\n  \"diffuse\": null,\n  \"specular_strength\": 0.5,\n  \"shininess\": 32.0\n}\n"
            // LuaScriptComponent ожидает таблицу с необязательными обработчиками.
            : "local M = {}\n\nfunction M.on_init(self)\nend\n\nfunction M.on_update(self, dt)\nend\n\nreturn M\n";
        if (!WriteNewFile(destination, contents, actionError)) return;
    }
    Refresh();
    selected = Utf8(DiskPath(current) / DiskPath(name));
    ImGui::CloseCurrentPopup();
}

void ContentBrowser::DrawCreateDialog()
{
    if (!ImGui::BeginPopupModal("Create resource", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(createType == CreateType::Folder ? "Folder"
        : createType == CreateType::Material ? "Material (.material.json)" : "Lua script (.lua)");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("Name", entryName, sizeof(entryName), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!actionError.empty()) ImGui::TextWrapped("%s", actionError.c_str());
    if (ImGui::Button("Create") || enter) CreateEntry();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ContentBrowser::DrawDeleteDialog()
{
    if (!ImGui::BeginPopupModal("Delete entry", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(pendingDelete.c_str());
    ImGui::TextUnformatted("Delete permanently? Only files and empty folders can be deleted.");
    if (!actionError.empty()) ImGui::TextWrapped("%s", actionError.c_str());
    if (ImGui::Button("Delete permanently")) {
        // Повторно проверяем папку и цель: список мог устареть после внешних изменений.
        const fs::path target = DiskPath(pendingDelete);
        if (!CheckDirectory()) actionError = error;
        else if (target.parent_path() != DiskPath(current) || pendingDelete == root)
            actionError = "Select an entry in the current directory.";
        else {
            const DWORD attributes = GetFileAttributesW(target.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) actionError = "Entry unavailable; cancel and refresh.";
            else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) actionError = "Deleting links is not supported.";
            else {
                // Без рекурсии: непустая папка не может удалить вложенные ресурсы.
                std::error_code ec;
                if (!fs::remove(target, ec)) actionError = ec ? "Cannot delete: " + ec.message() : "Entry no longer exists.";
                else { Refresh(); ImGui::CloseCurrentPopup(); }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ContentBrowser::Draw(bool& open)
{
    focused = false;
    if (!open) { wasOpen = false; return; }
    if (!wasOpen) Refresh();
    wasOpen = true;
    if (ImGui::Begin("Content Browser", &open)) {
        focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::BeginDisabled(current == root);
        if (ImGui::Button("Up")) { current = Utf8(DiskPath(current).parent_path()); Refresh(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) Refresh();
        ImGui::SameLine();
        ImGui::BeginDisabled(!available);
        if (ImGui::Button("Open in Explorer")) {
            // Повторно проверяем папку: после Refresh её могли удалить или переместить.
            if (CheckDirectory()) {
                const fs::path folder = DiskPath(current).make_preferred();
                const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
                    nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                // ShellExecute возвращает код <= 32 при ошибке запуска.
                if (result <= 32) error = "Cannot open folder in Explorer (code " + std::to_string(result) + ").";
                else error.clear();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!available);
        if (ImGui::Button("Add")) ImGui::OpenPopup("AddResourceMenu");
        bool requestedCreate = false;
        if (ImGui::BeginPopup("AddResourceMenu")) {
            if (ImGui::MenuItem("Folder")) { createType = CreateType::Folder; requestedCreate = true; }
            if (ImGui::MenuItem("Material (.json)")) { createType = CreateType::Material; requestedCreate = true; }
            if (ImGui::MenuItem("Lua script")) { createType = CreateType::LuaScript; requestedCreate = true; }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        // Модальный диалог открываем в области ID панели, после закрытия меню.
        if (requestedCreate) {
            entryName[0] = '\0';
            actionError.clear();
            ImGui::OpenPopup("Create resource");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!available || selected.empty());
        if (ImGui::Button("Delete")) {
            pendingDelete = selected;
            actionError.clear();
            ImGui::OpenPopup("Delete entry");
        }
        ImGui::EndDisabled();
        DrawCreateDialog();
        DrawDeleteDialog();
        ImGui::TextWrapped("%s", current.c_str());
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());

        if (rootMissing && ImGui::Button("Create resource root")) {
            std::error_code ec;
            fs::create_directory(DiskPath(root), ec);
            if (ec) error = "Cannot create root: " + ec.message();
            else { current = root; Refresh(); }
        }
        ImGui::Separator();
        // Переход откладываем до конца обхода, чтобы не инвалидировать записи.
        std::string requestedDirectory;
        if (ImGui::BeginChild("Files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
            if (available && entries.empty()) ImGui::TextDisabled("Empty folder");
            for (const Entry& entry : entries) {
                const std::string path = Utf8(DiskPath(current) / DiskPath(entry.name));
                ImGui::PushID(entry.name.c_str());
                // Имя рисуется отдельно: символы ## в имени не становятся ImGui ID.
                const ImVec2 rowPosition = ImGui::GetCursorPos();
                if (ImGui::Selectable("##entry", selected == path, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected = path;
                    if (entry.directory && !entry.link && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        requestedDirectory = path;
                }
                // Payload содержит копию UTF-8 пути, а не адрес временной строки.
                if (!entry.directory && !entry.link && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("GG_RESOURCE_PATH", path.c_str(), path.size() + 1);
                    ImGui::TextUnformatted(entry.name.c_str());
                    ImGui::EndDragDropSource();
                }
                const ImVec2 nextPosition = ImGui::GetCursorPos();
                ImGui::SetCursorPos(rowPosition);
                ImGui::TextUnformatted(entry.link ? "[Link]" : entry.directory ? "[Folder]"
                    : entry.mesh ? "[Mesh]" : FileType(entry.name));
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::SetCursorPos(nextPosition);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (!requestedDirectory.empty()) { current = requestedDirectory; Refresh(); }
        ImGui::TextUnformatted(selected.c_str());
    }
    ImGui::End();
}
