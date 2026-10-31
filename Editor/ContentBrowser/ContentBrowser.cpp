#include "ContentBrowser.h"
#include "EditorFileUtils.h"
#include "imgui.h"
#include "ResourceManager.h"
#include "core.h"
#include "Project.h"
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>

namespace fs = std::filesystem;
using EditorFiles::DiskPath;
using EditorFiles::Utf8Path;

namespace {
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
        if (lower.ends_with(".object.json")) return "[Object]";
        if (lower.ends_with(".level.json")) return "[Scene]";
        const auto dot = lower.find_last_of('.');
        const std::string extension = dot == std::string::npos ? "" : lower.substr(dot);
        if (extension == ".lua") return "[Lua]";
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg"
            || extension == ".bmp" || extension == ".tga" || extension == ".dds" || extension == ".hdr") return "[Image]";
        if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb") return "[Model]";
        if (extension == ".json") return "[JSON]";
        return "[File]";
    }

    int Category(const std::string& name, bool mesh)
    {
        const std::string type = FileType(name);
        if (type == "[Scene]") return 1;
        if (type == "[Object]") return 2;
        if (type == "[Material]") return 4;
        if (type == "[Image]") return 5;
        if (type == "[Lua]") return 6;
        return mesh ? 3 : 7;
    }

    std::wstring WideText(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (!count) return {};
        std::wstring result(count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
        return result;
    }

    bool Matches(const std::string& name, const std::wstring& query)
    {
        if (query.empty()) return true;
        const std::wstring wide = WideText(name);
        // Ordinal-сравнение поддерживает регистр кириллицы без изменения имён на диске.
        for (size_t at = 0; at + query.size() <= wide.size(); ++at)
            if (CompareStringOrdinal(wide.data() + at, static_cast<int>(query.size()), query.data(), static_cast<int>(query.size()), TRUE) == CSTR_EQUAL) return true;
        return false;
    }

    std::string FileSize(unsigned long long bytes)
    {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        if (bytes < 1024ull * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
        return std::to_string(bytes / (1024ull * 1024 * 1024)) + " GB";
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

ContentBrowser::ContentBrowser(const std::string& rootPath, const std::string& projectPath)
    : root(rootPath.empty() ? "" : Utf8Path(DiskPath(rootPath).lexically_normal())),
      projectRoot(projectPath), current(root)
{
    if (!root.empty()) navigation.push_back(root);
}

std::string ContentBrowser::ResourcePath(const std::string& path) const
{
    return projectRoot.empty() ? path : Utf8Path(DiskPath(path).lexically_relative(DiskPath(projectRoot)));
}

bool ContentBrowser::Navigate(const std::string& path, bool remember)
{
    const fs::path target = DiskPath(path).lexically_normal();
    const fs::path relative = target.lexically_relative(DiskPath(root));
    if (relative.empty() || relative.has_root_path()) { error = "Choose a folder inside the resource root."; return false; }
    for (const auto& part : relative) if (part == "..") { error = "Cannot leave the resource root."; return false; }
    const std::string previous = current;
    current = Utf8Path(target);
    if (!CheckDirectory()) { current = previous; return false; }
    if (remember && current != previous) {
        navigation.resize(navigationPosition + 1);
        navigation.push_back(current); navigationPosition = navigation.size() - 1;
    }
    search[0] = '\0'; selected.clear(); Refresh();
    return true;
}

void ContentBrowser::OpenFolder(const std::string& path)
{
    if (!CheckDirectory()) return;
    const fs::path folder = DiskPath(path).make_preferred();
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        error = "Cannot open folder in Explorer.";
    else error.clear();
}

void ContentBrowser::Activate(const Entry& entry, std::string& directory)
{
    if (entry.link || !CheckDirectory()) return;
    const std::string path = Utf8Path(DiskPath(current) / DiskPath(entry.name));
    const DWORD attributes = GetFileAttributesW(DiskPath(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) { error = "Entry changed or is unavailable; refresh to retry."; return; }
    error.clear();
    if (entry.directory) directory = path;
    else if (Category(entry.name, entry.mesh) == 1) sceneToOpen = path;
    else if (Category(entry.name, entry.mesh) == 2) objectToOpen = path;
}

bool ContentBrowser::CheckDirectory()
{
    // Не входим в junction/symlink, включая заменённую извне родительскую папку.
    for (fs::path path = DiskPath(current); !path.empty(); path = path.parent_path()) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            error = "Directory unavailable (Win32 " + std::to_string(GetLastError()) + "): " + Utf8Path(path);
            return false;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = "Expected a regular directory: " + Utf8Path(path);
            return false;
        }
        if (path == path.parent_path()) break;
    }
    return true;
}

void ContentBrowser::Refresh()
{
    entries.clear();
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
        entries.push_back({ Utf8Path(path.filename()), directory, link,
            !directory && !link && MeshResource::SupportsExtension(LowerAscii(Utf8Path(path.extension()))) });
        if (!directory && !link) {
            std::error_code sizeError;
            entries.back().bytes = fs::file_size(path, sizeError);
            entries.back().sizeKnown = !sizeError;
        }
        iterator.increment(ec);
        if (ec) { error = "Cannot read directory: " + ec.message(); entries.clear(); return; }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return a.name < b.name;
    });
    available = true;
    if (std::none_of(entries.begin(), entries.end(), [&](const Entry& entry) {
        return selected == Utf8Path(DiskPath(current) / DiskPath(entry.name));
    })) selected.clear();
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
    const char* suffix = createType == CreateType::Material ? ".material.json"
        : createType == CreateType::Scene ? ".level.json"
        : createType == CreateType::Object || createType == CreateType::MeshObject ? ".object.json" : nullptr;
    if (suffix && !LowerAscii(name).ends_with(suffix)) {
        if (LowerAscii(name).ends_with(".json")) name.resize(name.size() - 5);
        name += suffix;
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
        std::string contents = createType == CreateType::Material
            ? "{\n  \"version\": 1,\n  \"shader\": \"builtin/simple\",\n  \"render_state\": \"opaque\",\n"
              "  \"sampler\": \"wrap_linear\",\n  \"diffuse\": null,\n  \"specular_strength\": 0.5,\n  \"shininess\": 32.0\n}\n"
            // LuaScriptComponent ожидает таблицу с необязательными обработчиками.
            : "local M = {}\n\nfunction M.on_init(self)\nend\n\nfunction M.on_update(self, dt)\nend\n\nreturn M\n";
        if (createType == CreateType::Scene || createType == CreateType::Object || createType == CreateType::MeshObject) {
            rapidjson::Document document;
            rapidjson::Value description;
            // Используем сериализаторы движка: шаблоны не дублируют формат сцены и дерева.
            if (createType == CreateType::Scene) {
                Level scene;
                GameObject* cameraObject = scene.createObject();
                auto* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
                auto* navigation = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
                if (!camera || !navigation || !Project::PopulateDemoScene(scene)) {
                    actionError = "Cannot allocate the demo scene."; return;
                }
                // Камера сохраняется вместе с демкой, чтобы сцена открывалась с готовым обзором.
                cameraObject->getTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
                camera->bind_to_node(&cameraObject->getTransform());
                navigation->bind_to_node(&cameraObject->getTransform());
                description = scene.Serialize(document.GetAllocator(), actionError);
            } else {
                GameObject object;
                if (createType == CreateType::MeshObject) {
                    // Временный объект владеет компонентом; Init и создание GPU-ресурсов не нужны.
                    auto* mesh = object.createComponent<MeshComponent>();
                    if (!mesh) { actionError = "Cannot allocate mesh component."; return; }
                    mesh->bind_to_node(&object.getTransform());
                }
                description = object.Serialize(document.GetAllocator(), actionError);
            }
            if (description.IsNull()) return;
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            if (!description.Accept(writer)) { actionError = "Cannot serialize resource."; return; }
            contents.assign(buffer.GetString(), buffer.GetSize());
            contents += '\n';
        }
        if (!WriteNewFile(destination, contents, actionError)) return;
    }
    search[0] = '\0'; typeFilter = 0;
    Refresh();
    selected = Utf8Path(DiskPath(current) / DiskPath(name));
    ImGui::CloseCurrentPopup();
}

void ContentBrowser::DrawCreateDialog()
{
    if (!ImGui::BeginPopupModal("Create resource", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(createType == CreateType::Folder ? "Folder"
        : createType == CreateType::Scene ? "Scene (.level.json)"
        : createType == CreateType::MeshObject ? "Mesh object (.object.json)"
        : createType == CreateType::Object ? "Object (.object.json)"
        : createType == CreateType::Material ? "Material (.material.json)" : "Lua script (.lua)");
    if (createType == CreateType::MeshObject) ImGui::TextDisabled("Object with a cube mesh component.");
    if (createType == CreateType::Scene) ImGui::TextDisabled("Default demo with camera, lights and primitives. Double-click the file to open.");
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

void ContentBrowser::DrawCreateMenu(DrawRequests& requests)
{
    if (ImGui::MenuItem("Folder")) { createType = CreateType::Folder; requests.create = true; }
    ImGui::Separator();
    if (ImGui::MenuItem("Scene")) { createType = CreateType::Scene; requests.create = true; }
    if (ImGui::MenuItem("Mesh object")) { createType = CreateType::MeshObject; requests.create = true; }
    if (ImGui::MenuItem("Object")) { createType = CreateType::Object; requests.create = true; }
    if (ImGui::MenuItem("Material")) { createType = CreateType::Material; requests.create = true; }
    if (ImGui::MenuItem("Lua script")) { createType = CreateType::LuaScript; requests.create = true; }
}

void ContentBrowser::DrawToolbar(DrawRequests& requests)
{
    // В низкой панели оставляем одну строку; остальные настройки раскрываются по кнопке.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 3));
    const bool showPath = ImGui::GetContentRegionAvail().y >= 5 * ImGui::GetFrameHeightWithSpacing();
    const bool wide = ImGui::GetContentRegionAvail().x >= 240;
    ImGui::BeginDisabled(!available);
    if (ImGui::SmallButton("Add")) ImGui::OpenPopup("Add resource");
    if (ImGui::BeginPopup("Add resource")) {
        DrawCreateMenu(requests);
        ImGui::EndPopup();
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (wide) {
        ImGui::BeginDisabled(navigationPosition == 0);
        if (ImGui::SmallButton("<") && Navigate(navigation[navigationPosition - 1], false)) --navigationPosition;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Back");
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(navigationPosition + 1 >= navigation.size());
        if (ImGui::SmallButton(">") && Navigate(navigation[navigationPosition + 1], false)) ++navigationPosition;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Forward");
        ImGui::EndDisabled(); ImGui::SameLine();
    }
    ImGui::BeginDisabled(current == root);
    if (ImGui::SmallButton("^")) Navigate(Utf8Path(DiskPath(current).parent_path()));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Parent folder");
    ImGui::EndDisabled(); ImGui::SameLine();
    const float menuWidth = ImGui::CalcTextSize("...").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - menuWidth - ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##search", "Search...", search, sizeof(search))) selected.clear();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nSearch this folder. F5: refresh | Enter: open | Del: delete", current.c_str());
    ImGui::SameLine();

    if (ImGui::SmallButton("...")) ImGui::OpenPopup("Browser options");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("View, filters and file actions");
    if (ImGui::BeginPopup("Browser options")) {
        if (!wide) {
            if (ImGui::MenuItem("Back", nullptr, false, navigationPosition > 0)
                && Navigate(navigation[navigationPosition - 1], false)) --navigationPosition;
            if (ImGui::MenuItem("Forward", nullptr, false, navigationPosition + 1 < navigation.size())
                && Navigate(navigation[navigationPosition + 1], false)) ++navigationPosition;
        }
        if (ImGui::MenuItem("Refresh", "F5")) requests.refresh = true;
        if (ImGui::MenuItem("Open in Explorer", nullptr, false, available)) OpenFolder(current);
        if (ImGui::MenuItem("Clear search")) { search[0] = '\0'; selected.clear(); }
        ImGui::Separator();
        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("Type", &typeFilter, "All files\0Scenes\0Objects\0Models\0Materials\0Textures\0Scripts\0Other\0")) selected.clear();
        if (ImGui::MenuItem("List", nullptr, !tiles)) tiles = false;
        if (ImGui::MenuItem("Tiles", nullptr, tiles)) tiles = true;
        if (ImGui::BeginMenu("New", available)) { DrawCreateMenu(requests); ImGui::EndMenu(); }
        if (ImGui::MenuItem("Delete...", "Del", false, available && !selected.empty())) requests.remove = true;
        if (rootMissing && ImGui::MenuItem("Create resource root")) {
            std::error_code ec;
            fs::create_directory(DiskPath(root), ec);
            if (ec) error = "Cannot create resource root: " + ec.message();
            else { current = root; requests.refresh = true; }
        }
        if (!error.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", error.c_str()); }
        ImGui::EndPopup();
    }
    if (showPath) {
        // Путь занимает одну строку, а список предков открывается только по запросу.
        ImGui::SetNextItemWidth(-1);
        const std::string relative = Utf8Path(DiskPath(current).lexically_relative(DiskPath(root)));
        const std::string label = relative == "." ? Utf8Path(DiskPath(root).filename())
            : Utf8Path(DiskPath(root).filename()) + "/" + relative;
        if (ImGui::BeginCombo("##folder", label.c_str())) {
            for (fs::path folder = DiskPath(current); ; folder = folder.parent_path()) {
                const std::string path = Utf8Path(folder);
                ImGui::PushID(path.c_str());
                if (ImGui::Selectable(path.c_str(), path == current)) requests.directory = path;
                ImGui::PopID();
                if (path == root || folder == folder.parent_path()) break;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", current.c_str());
    }
    ImGui::PopStyleVar(2);
    if (!requests.directory.empty()) { Navigate(requests.directory); requests.directory.clear(); }
}

size_t ContentBrowser::DrawFileList(float footerHeight, DrawRequests& requests)
{
    const std::wstring query = WideText(search);
    size_t visible = 0;
    if (ImGui::BeginChild("Files", ImVec2(0, -footerHeight))) {
        const int columns = tiles ? std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / 132.0f), 1, 32) : 3;
        const ImGuiTableFlags flags = tiles ? ImGuiTableFlags_SizingStretchSame
            : ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable(tiles ? "Tiles" : "List", columns, flags)) {
            if (!tiles) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableHeadersRow();
            }
            for (const Entry& entry : entries) {
                if (!Matches(entry.name, query) || (!entry.directory && typeFilter != 0 && Category(entry.name, entry.mesh) != typeFilter)) continue;
                ++visible;
                ImGui::TableNextColumn();
                const std::string path = Utf8Path(DiskPath(current) / DiskPath(entry.name));
                const char* type = entry.link ? "[Link]" : entry.directory ? "[Folder]" : entry.mesh ? "[Mesh]" : FileType(entry.name);
                ImGui::PushID(entry.name.c_str());
                const ImVec2 position = ImGui::GetCursorScreenPos();
                const ImVec2 itemSize = tiles ? ImVec2((std::max)(32.0f, ImGui::GetContentRegionAvail().x), 96) : ImVec2(0, ImGui::GetTextLineHeightWithSpacing());
                if (ImGui::Selectable("##entry", selected == path, ImGuiSelectableFlags_AllowDoubleClick | (tiles ? 0 : ImGuiSelectableFlags_SpanAllColumns), itemSize)) {
                    selected = path;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) Activate(entry, requests.directory);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) selected = path;
                const ImVec2 after = ImGui::GetCursorScreenPos();
                const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
                if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImGui::BeginTooltip(); ImGui::TextUnformatted(entry.name.c_str()); ImGui::TextUnformatted(type);
                    if (entry.sizeKnown) ImGui::TextUnformatted(FileSize(entry.bytes).c_str());
                    ImGui::EndTooltip();
                }
                // Payload содержит путь относительно проекта; загрузкой занимается принимающее поле.
                if (!entry.directory && !entry.link && ImGui::BeginDragDropSource()) {
                    const std::string resource = ResourcePath(path);
                    ImGui::SetDragDropPayload("GG_RESOURCE_PATH", resource.c_str(), resource.size() + 1);
                    ImGui::TextUnformatted(entry.name.c_str()); ImGui::EndDragDropSource();
                }
                if (ImGui::BeginPopupContextItem("Entry menu")) {
                    const bool canOpen = !entry.link && (entry.directory || Category(entry.name, entry.mesh) == 1 || Category(entry.name, entry.mesh) == 2);
                    if (ImGui::MenuItem("Open", "Enter", false, canOpen)) Activate(entry, requests.directory);
                    if (ImGui::MenuItem("Open containing folder")) OpenFolder(current);
                    if (ImGui::MenuItem("Copy project-relative path")) ImGui::SetClipboardText(ResourcePath(path).c_str());
                    if (ImGui::MenuItem("Copy absolute path")) ImGui::SetClipboardText(path.c_str());
                    if (ImGui::BeginMenu("New", available)) { DrawCreateMenu(requests); ImGui::EndMenu(); }
                    if (ImGui::MenuItem("Delete...", "Del", false, !entry.link)) { selected = path; requests.remove = true; }
                    ImGui::EndPopup();
                }
                // Имя не используется как ImGui label: символы ## остаются видимыми.
                if (tiles) {
                    auto* draw = ImGui::GetWindowDrawList();
                    const ImVec4 clip(minimum.x + 6, minimum.y + 4, maximum.x - 6, maximum.y - 4);
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(minimum.x + 8, minimum.y + 10), ImGui::GetColorU32(ImGuiCol_TextDisabled), type, nullptr, 0, &clip);
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(minimum.x + 8, minimum.y + 36), ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str(), nullptr, maximum.x - minimum.x - 16, &clip);
                } else {
                    ImGui::SetCursorScreenPos(position); ImGui::TextUnformatted(entry.name.c_str());
                    ImGui::SetCursorScreenPos(after);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(type);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.sizeKnown ? FileSize(entry.bytes).c_str() : "--");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (available && visible == 0) ImGui::TextDisabled(entries.empty() ? "Empty folder" : "No matching entries");
        if (ImGui::BeginPopupContextWindow("Browser background", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("New", available)) { DrawCreateMenu(requests); ImGui::EndMenu(); }
            if (ImGui::MenuItem("Refresh", "F5")) requests.refresh = true;
            if (ImGui::MenuItem("Open in Explorer", nullptr, false, available)) OpenFolder(current);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    return visible;
}

void ContentBrowser::ApplyFileActions(DrawRequests& requests)
{
    // Refresh и навигация выполняются после обхода: они инвалидируют entries.
    const bool keyboard = focused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (keyboard) {
        if (ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteFocused)) requests.refresh = true;
        if (available && !selected.empty() && ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteFocused)) requests.remove = true;
        if (ImGui::Shortcut(ImGuiKey_Enter, ImGuiInputFlags_RouteFocused))
            for (const auto& entry : entries) if (selected == Utf8Path(DiskPath(current) / DiskPath(entry.name))) { Activate(entry, requests.directory); break; }
    }
    if (requests.create) { entryName[0] = '\0'; actionError.clear(); ImGui::OpenPopup("Create resource"); }
    if (requests.remove) { pendingDelete = selected; actionError.clear(); ImGui::OpenPopup("Delete entry"); }
    DrawCreateDialog(); DrawDeleteDialog();
    if (!requests.directory.empty()) Navigate(requests.directory);
    else if (requests.refresh) Refresh();
}

void ContentBrowser::Draw(bool& open)
{
    focused = false;
    if (!open) { wasOpen = false; return; }
    if (root.empty()) {
        if (ImGui::Begin("Content Browser", &open)) ImGui::TextDisabled("Create or open a project from the File menu.");
        ImGui::End(); return;
    }
    if (!wasOpen) Refresh();
    wasOpen = true;
    if (ImGui::Begin("Content Browser", &open)) {
        focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawRequests requests;
        DrawToolbar(requests);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const bool showStatus = ImGui::GetContentRegionAvail().y >= 3 * lineHeight;
        const float footerHeight = showStatus ? lineHeight : 0.0f;
        const size_t visible = DrawFileList(footerHeight, requests);
        ApplyFileActions(requests);
        if (showStatus) {
            ImGui::TextDisabled("%zu / %zu", visible, entries.size());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(selected.empty() ? current.c_str() : selected.c_str());
                ImGui::TextUnformatted("F5: refresh | Enter: open | Del: delete");
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::End();
}
