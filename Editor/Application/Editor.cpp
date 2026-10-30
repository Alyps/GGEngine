#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include "Editor.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "RenderPresets.h"
#include <cmath>
#include <DirectXCollision.h>
#include <filesystem>
#include <shobjidl.h>
#include <shellapi.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using DirectX::SimpleMath::Vector4;

namespace {
	constexpr wchar_t EditorProperty[] = L"GGEngine.Editor";

	void CollectNodes(TransformNode& node, std::vector<TransformNode*>& nodes)
	{
		if (node.isPendingDestroy()) return;
		nodes.push_back(&node);
		for (auto* child : node.get_children()) if (child) CollectNodes(*child, nodes);
	}
	std::string NodePath(const TransformNode& node)
	{
		std::string result = node.get_name();
		for (auto* parent = node.get_parent(); parent; parent = parent->get_parent()) result = parent->get_name() + "/" + result;
		return result;
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

	bool PopulateDemoScene(Level& level)
	{
		// Уровень владеет объектами и освободит частично созданную сцену при отказе.
		GameObject* objects[6]{};
		for (auto& object : objects) {
			object = level.createObject();
			if (!object) return false;
		}
		auto* cube = objects[0]->createComponent<MeshComponent>();
		auto* floor = objects[1]->createComponent<MeshComponent>(MeshPresets::CreateQuad(5.0f, 7.0f));
		auto* secondCube = objects[2]->createComponent<MeshComponent>(true);
		auto* sphere = objects[3]->createComponent<MeshComponent>(MeshPresets::CreateCircle(0.5f));
		auto* directional = objects[4]->createComponent<DirectionalLightComponent>();
		auto* point = objects[5]->createComponent<PointLightComponent>();
		if (!cube || !floor || !secondCube || !sphere || !directional || !point) return false;

		// Повторяем расстановку прежней демонстрации: два куба, шар и пол XZ.
		objects[1]->getTransform().set_position(DXVector3(0.0f, -0.5f, 0.0f));
		objects[1]->getTransform().set_rotation(
			DXQuaternion::CreateFromAxisAngle(DXVector3::UnitX, -DirectX::XM_PIDIV2));
		objects[2]->getTransform().set_position(DXVector3(-1.5f, 0.0f, 0.0f));
		objects[3]->getTransform().set_position(DXVector3(1.5f, 0.0f, 0.0f));
		cube->bind_to_node(&objects[0]->getTransform());
		floor->bind_to_node(&objects[1]->getTransform());
		secondCube->bind_to_node(&objects[2]->getTransform());
		sphere->bind_to_node(&objects[3]->getTransform());

		// Синий направленный и зелёный точечный свет сохраняют вид исходной сцены.
		objects[4]->getTransform().set_rotation(
			DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0.0f));
		directional->color = { 0.0f, 0.0f, 1.0f };
		directional->intensity = 0.6f;
		directional->bind_to_node(&objects[4]->getTransform());
		objects[5]->getTransform().set_position(DXVector3(2.0f, 2.0f, -2.0f));
		point->color = { 0.0f, 1.0f, 0.0f };
		point->intensity = 12.0f;
		point->range = 5.0f;
		point->bind_to_node(&objects[5]->getTransform());
		return true;
	}

	std::filesystem::path ProjectDiskPath(const std::string& text) {
		return std::filesystem::path(std::u8string(text.begin(), text.end()));
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

	bool ClipSelectionEdge(Vector4& a, Vector4& b)
	{
		// Отсекаем ребро в clip space до деления на w, в том числе за камерой.
		const float start[] = { a.x + a.w, a.w - a.x, a.y + a.w, a.w - a.y, a.z, a.w - a.z };
		const float end[] = { b.x + b.w, b.w - b.x, b.y + b.w, b.w - b.y, b.z, b.w - b.z };
		float first = 0.0f, last = 1.0f;
		for (int plane = 0; plane < 6; ++plane) {
			if (start[plane] < 0 && end[plane] < 0) return false;
			if ((start[plane] < 0) != (end[plane] < 0)) {
				const float t = start[plane] / (start[plane] - end[plane]);
				if (start[plane] < 0) first = (std::max)(first, t);
				else last = (std::min)(last, t);
			}
		}
		if (first > last) return false;
		const Vector4 delta = b - a;
		b = a + delta * last;
		a += delta * first;
		return a.w > 1e-6f && b.w > 1e-6f;
	}

	// Матрица родителя фильтруется так же, как в TransformNode::worldMatrix.
	bool ParentMatrix(const TransformNode& node, DXMatrix& result)
	{
		result = DXMatrix::Identity;
		if (!node.get_parent()) return true;
		DXVector3 scale, position;
		DXQuaternion rotation;
		if (!DXMatrix(node.get_parent()->worldMatrix()).Decompose(scale, rotation, position)) return false;
		result = DXMatrix::CreateScale(node.is_inherit_scale() ? scale : DXVector3::One)
			* DXMatrix::CreateFromQuaternion(node.is_inherit_rotation() ? rotation : DXQuaternion::Identity)
			* DXMatrix::CreateTranslation(node.is_inherit_position() ? position : DXVector3::Zero);
		return std::abs(result.Determinant()) > 1e-8f;
	}
}

Editor::Editor(Game& application)
	: game(application), contentBrowser("")
{
	recentProjects.Load(projectError);
	// Демо и ресурсы доступны сразу, до выбора постоянного места проекта.
	std::string error;
	if (Project::CreateTemporary(pendingProject, error)) {
		initializeDemo = true;
		requestedProjectSwitch = true;
	} else projectError = error;
}

bool Editor::LaunchGame(const std::string& executablePath, const std::string& projectDirectory,
	const std::string& startLevelPath, std::string& error)
{
	error.clear();
	const auto toWide = [&error](const std::string& path, std::wstring& wide) {
		// Не допускаем скрытого обрезания пути и потери символов при преобразовании.
		if (path.empty() || path.find('\0') != std::string::npos || path.size() > 32767) {
			error = "Invalid or empty launch path";
			return false;
		}
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			path.data(), static_cast<int>(path.size()), nullptr, 0);
		if (!count) { error = "Invalid UTF-8 launch path"; return false; }
		wide.resize(count);
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
			static_cast<int>(path.size()), wide.data(), count)) {
			error = "Cannot convert launch path";
			return false;
		}
		return true;
	};
	std::wstring executable, project, scene, standard;
	if (!toWide(executablePath, executable) || !toWide(projectDirectory, project) ||
		!toWide(startLevelPath, scene) ||
		!toWide(game.getResourceManager().GetStandardResourceDirectory(), standard)) return false;

	// Разрешаем пути до смены cwd дочернего процесса; cwd редактора не меняется.
	for (std::wstring* path : { &executable, &project, &standard }) {
		std::error_code code;
		const auto absolute = std::filesystem::absolute(std::filesystem::path(*path), code);
		if (code) { error = "Cannot resolve launch path: " + code.message(); return false; }
		*path = absolute.native();
	}
	const auto quote = [](const std::wstring& argument) {
		std::wstring result = L"\"";
		size_t slashes = 0;
		// CRT удваивает обратные слеши перед кавычкой, включая закрывающую.
		for (wchar_t character : argument) {
			if (character == L'\\') { ++slashes; continue; }
			result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
			result += character;
			slashes = 0;
		}
		result.append(slashes * 2, L'\\');
		return result + L'"';
	};
	std::wstring command = quote(executable) + L" --project " + quote(project) +
		L" --scene " + quote(scene) + L" --standard-resources " + quote(standard);
	if (command.size() >= 32767) { error = "Game command line is too long"; return false; }

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	// Отдельная консоль показывает ошибки игры; дескрипторы редактора не наследуются.
	if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
		CREATE_NEW_CONSOLE, nullptr, project.c_str(), &startup, &process)) {
		error = "Cannot launch Game.exe (Win32 " + std::to_string(GetLastError()) + ")";
		return false;
	}
	// Закрытие дескрипторов не останавливает игру; ожидание не блокирует редактор.
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

CameraMovementComponent* Editor::GetCameraMovement() const
{
	// Компоненты теперь удаляемые: получаем навигацию живой активной камеры.
	EditorCamera* camera = game.getRenderServer().getCamera();
	if (!camera || camera->isPendingDestroy() || !camera->getOwner()
		|| camera->getOwner()->isPendingDestroy()) return nullptr;
	for (Component* component : camera->getOwner()->getComponents()) {
		auto* movement = dynamic_cast<CameraMovementComponent*>(component);
		if (movement && !movement->isPendingDestroy()
			&& movement->getTransformNode() == camera->getTransformNode()) return movement;
	}
	return nullptr;
}

Editor::~Editor()
{
	// Возвращаем оконную процедуру до удаления контекста ImGui.
	const HWND window = game.getWindow().getWindow();
	if (previousProcedure && IsWindow(window))
		SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProcedure));
	RemovePropW(window, EditorProperty);
	if (auto* movement = GetCameraMovement()) movement->enabled = true;
	ReleaseViewport();
	if (ui) {
		ImGui::SetCurrentContext(ui);
		if (rendererReady) ImGui_ImplDX11_Shutdown();
		if (platformReady) ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext(ui);
	}
}

bool Editor::Init()
{
	if (ui) return platformReady && rendererReady && previousProcedure;
	IMGUI_CHECKVERSION();
	ui = ImGui::CreateContext();
	if (!ui) return false;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	// Пока нет проекта сцены, не создаём imgui.ini в исходниках.
	io.IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGui::GetStyle().WindowRounding = 3.0f;
	ImGui::GetStyle().FrameRounding = 3.0f;

	const HWND window = game.getWindow().getWindow();
	platformReady = ImGui_ImplWin32_Init(window);
	if (!platformReady) return false;
	rendererReady = ImGui_ImplDX11_Init(game.getRenderServer().Device(), game.getRenderServer().Context());
	if (!rendererReady || !SetPropW(window, EditorProperty, this)) return false;
	// Движок не зависит от ImGui: перехватчик существует только в редакторе.
	SetLastError(0);
	previousProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC,
		reinterpret_cast<LONG_PTR>(&Editor::HandleMessage)));
	if (!previousProcedure) return false;
	if (auto* movement = GetCameraMovement()) movement->enabled = false;
	return true;
}

LRESULT CALLBACK Editor::HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	auto* editor = static_cast<Editor*>(GetPropW(window, EditorProperty));
	if (!editor) return DefWindowProcW(window, message, wparam, lparam);
	// Не передаём WM_CLOSE в движок: окно должно жить до решения пользователя.
	if (message == WM_CLOSE) { editor->RequestClose(); return 0; }
	if (message == WM_INPUT && editor->navigating) editor->checkSceneDirty = true;
	ImGui::SetCurrentContext(editor->ui);
	ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
	// WM_INPUT доходит до движка, чтобы отпускания клавиш не терялись.
	if (message == WM_KILLFOCUS) {
		editor->navigating = false;
		if (auto* movement = editor->GetCameraMovement()) movement->enabled = false;
	}
	return CallWindowProcW(editor->previousProcedure, window, message, wparam, lparam);
}

int Editor::Run()
{
	if (!Init()) {
		std::cerr << "Editor UI initialization failed\n";
		return 1;
	}
	ApplyProjectAction();
	game.getTimeManager().reset();
	for (;;) {
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		// При сворачивании не создаём цели нулевого размера и не тратим GPU.
		const HWND window = game.getWindow().getWindow();
		if (IsIconic(window)) {
			if (auto* movement = GetCameraMovement()) movement->enabled = false;
			WaitMessage();
			game.getTimeManager().reset();
			continue;
		}
		RECT client{};
		GetClientRect(window, &client);
		if (!game.getRenderServer().ResizeMainTarget(client.right, client.bottom)) {
			std::cerr << "Cannot resize editor back buffer\n";
			return 1;
		}
		game.BeginFrame();
		// Физика работает также при скрытом viewport.
		if (!editedObject && !ImGui::GetTopMostPopupModal() && !game.StepPhysics()) return 1;
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
		scenePanelFocused = false;
		saveShortcut = false;
		// Ctrl+S доступен в полях инспектора; текст пути подтверждается до записи сцены.
		if (!navigating && GetForegroundWindow() == window
			&& !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
			saveShortcut = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
		if (saveShortcut) requestedSave = true;
		DrawDockspace();
		DrawProjectDialogs();
		if (showHierarchy) DrawHierarchy();
		if (showInspector) DrawInspector();
		DrawViewport();
		contentBrowser.Draw(showContentBrowser);
		if (const auto file = contentBrowser.TakeObjectToOpen(); !file.empty()) QueueObjectOpen(file);
		if (const auto file = contentBrowser.TakeSceneToOpen(); !file.empty()) QueueSceneOpen(file);
		// Закрываем жест также при смене выбора или скрытии панели.
		if (editingTransform && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing()) FinishTransformEdit();
		HandleShortcuts();
		ImGui::Render();
		game.getRenderServer().BeginMainPass();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		game.getRenderServer().Present();
		// Структурные операции выполняются после обходов и рисования текущего кадра.
		ApplyTreeAction();
		ApplySceneActions();
		if (requestedObjectExport && requestedObjectExport->isPendingDestroy()) requestedObjectExport = nullptr;
		if (Level* level = game.getLevel()) {
			// История и выбор заимствуют адреса: очищаем их до освобождения объектов.
			// Без постоянных ID нельзя безопасно хранить Undo через удаление сцены.
			for (GameObject* object : level->getObjects()) {
				if (!object) continue;
                // Удалённый компонент не должен оставлять черновик для повторно занятого адреса.
                for (Component* component : object->getComponents())
                    if (component && component->isPendingDestroy())
                        if (auto* mesh = dynamic_cast<MeshComponent*>(component)) meshPathDrafts.erase(mesh);
                if (!object->isPendingDestroy()) continue;
				meshPathDrafts.clear();
				if (selectedObject == object) {
					selectedObject = nullptr;
					selectedNode = nullptr;
				}
				history.clear();
				historyPosition = 0;
				editingTransform = editingGizmo = false;
				pendingEdit = {};
			}
		}
		game.EndFrame();
		ApplyDocumentActions();
		ApplyProjectAction();
		ApplyObjectActions();
	}
}

void Editor::DrawDockspace()
{
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (editedObject && ImGui::MenuItem("Return to Scene")) { pendingAction = PendingAction::ReturnScene; checkPendingAction = true; }
			if (ImGui::MenuItem(editedObject ? "Save Object" : "Save Scene", "Ctrl+S", false, game.getLevel() != nullptr)) requestedSave = true;
			if (ImGui::MenuItem("Save selected object as...", nullptr, false, selectedObject != nullptr)) {
				if (PickObjectSavePath(game.getWindow().getWindow(), exportObjectPath, documentError)) requestedObjectExport = selectedObject;
				game.getTimeManager().reset();
			}
			// Сначала возвращаемся к сцене: она может иметь собственные несохранённые изменения.
			ImGui::BeginDisabled(editedObject != nullptr);
			if (ImGui::MenuItem("New Project...")) {
				pendingAction = PendingAction::NewProject;
				checkPendingAction = true;
			}
			if (ImGui::MenuItem("Open Project...")) {
				std::string file;
				if (PickProjectPath(game.getWindow().getWindow(), false, file, projectError)) QueueProjectOpen(file);
				game.getTimeManager().reset();
			}
			if (ImGui::BeginMenu("Recent Projects", !recentProjects.Entries().empty())) {
				for (const auto& file : recentProjects.Entries()) {
					ImGui::PushID(file.c_str());
					// Путь рисуется отдельно: ## внутри имени файла не меняет подпись.
					if (ImGui::MenuItem("Open")) QueueProjectOpen(file);
					ImGui::SameLine(); ImGui::TextUnformatted(file.c_str());
					ImGui::PopID();
				}
				ImGui::EndMenu();
			}
			ImGui::EndDisabled();
			if (ImGui::MenuItem("Open Project Folder", nullptr, false, !project.filePath.empty())) {
				const auto folder = ProjectDiskPath(project.Root()).make_preferred();
				if (reinterpret_cast<INT_PTR>(ShellExecuteW(game.getWindow().getWindow(), L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
					projectError = "Cannot open project folder.";
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) RequestClose();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo transform", "Ctrl+Z", false, !editingTransform && historyPosition > 0)) Undo();
			if (ImGui::MenuItem("Redo transform", "Ctrl+Y", false,
				!editingTransform && historyPosition < history.size())) Redo();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window")) {
			ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
			ImGui::MenuItem("Inspector", nullptr, &showInspector);
			ImGui::MenuItem("Content Browser", nullptr, &showContentBrowser);
			if (ImGui::MenuItem("Reset layout")) resetLayout = true;
			ImGui::EndMenu();
		}
		ImGui::TextDisabled("  GGEngine | %.1f ms", ImGui::GetIO().DeltaTime * 1000.0f);
		if (!project.filePath.empty()) { ImGui::SameLine(); ImGui::TextUnformatted(temporaryProject ? "Unsaved project" : project.name.c_str()); }
		if (sceneDirty) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
		if (editedObject) { ImGui::SameLine(); ImGui::TextUnformatted(objectFile.c_str()); }
		else if (!sceneFile.empty()) { ImGui::SameLine(); ImGui::TextUnformatted(sceneFile.c_str()); }
		ImGui::EndMainMenuBar();
	}
	const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
	if (!layoutInitialized || resetLayout) {
		// Начальная раскладка строится один раз, затем панели свободно перемещаются.
		resetLayout = false;
		layoutInitialized = true;
		ImGui::DockBuilderRemoveNode(dock);
		ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
		ImGuiID center = dock;
		const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
		ImGui::DockBuilderDockWindow("Content Browser", bottom);
		ImGui::DockBuilderDockWindow("Hierarchy", left);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderFinish(dock);
	}
}

void Editor::QueueProjectOpen(const std::string& file)
{
	if (editedObject) { documentError = "Return to Scene before switching projects."; return; }
	if (!Project::Open(file, pendingProject, projectError)) return;
	pendingAction = PendingAction::OpenProject;
	checkPendingAction = true;
}

void Editor::DrawProjectDialogs()
{
	if (showNewProject) {
		showNewProject = false; projectCreationCompleted = false;
		projectCreationError.clear(); projectCreationTarget = {};
		ImGui::OpenPopup("New project");
	}
	if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(creationSavesCurrentScene ? "Save the current project" : "Create a project with the default demo scene");
		if (ImGui::InputText("Name", newProjectName, sizeof(newProjectName))) { projectCreationError.clear(); projectCreationTarget = {}; }
		if (ImGui::InputText("Parent folder", newProjectParent, sizeof(newProjectParent))) { projectCreationError.clear(); projectCreationTarget = {}; }
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			std::string folder;
			projectCreationError.clear();
			if (PickProjectPath(game.getWindow().getWindow(), true, folder, projectCreationError)) {
				projectCreationTarget = {};
				if (folder.size() < sizeof(newProjectParent)) strcpy_s(newProjectParent, folder.c_str());
				else projectCreationError = "Selected folder path is too long.";
			}
			game.getTimeManager().reset();
		}
		ImGui::TextDisabled("A new folder with the project name will be created here.");
		ImGui::TextWrapped("%s", creationSavesCurrentScene
			? "The current scene and resource files will be saved in the selected location."
			: "A fresh demo scene will be saved. Current objects and resources will not be copied.");
		if (!projectCreationError.empty()) ImGui::TextWrapped("%s", projectCreationError.c_str());
		ImGui::BeginDisabled(projectCreationCompleted || requestedProjectCreation);
		if (ImGui::Button(projectCreationTarget.filePath.empty() ? (creationSavesCurrentScene ? "Save" : "Create") : "Retry saving scene")) requestedProjectCreation = true;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			resumeAfterSave = false; pendingAction = PendingAction::None;
			ImGui::CloseCurrentPopup();
		}
		if (projectCreationCompleted) { projectCreationCompleted = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if (showUnsavedDialog) { showUnsavedDialog = false; documentError.clear(); ImGui::OpenPopup("Unsaved scene"); }
	if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::BeginDisabled(closeUnsavedDialog);
		const char* next = pendingAction == PendingAction::Close ? "closing the editor"
			: pendingAction == PendingAction::NewProject ? "creating a new project"
			: pendingAction == PendingAction::ReturnScene ? "returning to the scene"
			: pendingAction == PendingAction::OpenScene ? "opening another scene"
			: pendingAction == PendingAction::OpenObject ? "opening another object" : "opening another project";
		ImGui::TextWrapped("Save the current %s before %s?", editedObject ? "object" : "scene", next);
		if (!documentError.empty()) ImGui::TextWrapped("%s", documentError.c_str());
		if (ImGui::Button("Save")) {
			resumeAfterSave = true;
			if (!editedObject && temporaryProject) { creationSavesCurrentScene = true; showNewProject = true; ImGui::CloseCurrentPopup(); }
			else requestedSave = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't save")) { proceedPendingAction = true; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			pendingAction = PendingAction::None; resumeAfterSave = closeAfterReturn = openSceneAfterReturn = false; ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		if (closeUnsavedDialog) { closeUnsavedDialog = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if ((!projectError.empty() || !documentError.empty()) && !ImGui::GetTopMostPopupModal()) {
		ImGui::Begin("Project message", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::TextWrapped("%s", projectError.c_str());
		ImGui::TextWrapped("%s", documentError.c_str());
		if (ImGui::Button("Dismiss")) { projectError.clear(); documentError.clear(); }
		ImGui::End();
	}
}

void Editor::RequestClose()
{
	// При закрытии проверяются оба документа: сначала объект, затем припаркованная сцена.
	closeAfterReturn = editedObject != nullptr;
	pendingAction = editedObject ? PendingAction::ReturnScene : PendingAction::Close;
	checkPendingAction = true;
	// Закрытие через панель задач должно показывать диалог и для свёрнутого окна.
	if (IsIconic(game.getWindow().getWindow())) ShowWindow(game.getWindow().getWindow(), SW_RESTORE);
}

void Editor::HandleShortcuts()
{
	// Не регистрируем Z/Y глобально во время ввода: ими пользуется текстовый редактор ImGui.
	if (GetForegroundWindow() != game.getWindow().getWindow() || navigating || ImGuizmo::IsUsing()
		|| ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()
		|| ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
	if (!scenePanelFocused || contentBrowser.IsFocused()) return;
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) Undo();
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) Redo();
	if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
		if (selectedNode && selectedNode->get_parent()) treeAction = { TreeOperation::Remove, selectedNode };
		else if (CanDeleteObject(selectedObject)) requestedDelete = selectedObject;
	}
}

bool Editor::CaptureScene(std::string& snapshot, std::string& error) const
{
	Level* level = game.getLevel();
	if (!level) { error = "No scene is open."; return false; }
	rapidjson::Document document;
	auto json = editedObject ? editedObject->Serialize(document.GetAllocator(), error) : level->Serialize(document.GetAllocator(), error);
	if (json.IsNull()) return false;
	rapidjson::StringBuffer bytes;
	rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);
	if (!json.Accept(writer)) { error = "Cannot serialize the current scene."; return false; }
	snapshot.assign(bytes.GetString(), bytes.GetSize());
	return true;
}

void Editor::RefreshSceneDirty()
{
	checkSceneDirty = false;
	std::string snapshot, error;
	sceneDirty = !CaptureScene(snapshot, error) || savedScene.empty() || snapshot != savedScene;
}

bool Editor::SaveScene(Project& destination, std::string& error)
{
	std::string snapshot;
	if (!CaptureScene(snapshot, error)) return false;
	Project saved = destination;
	const std::string activeScene = !sceneFile.empty() ? sceneFile
		: !saved.startupScene.empty() ? saved.startupScene : saved.resourceDirectory + "/Scenes/Main.level.json";
	if (saved.startupScene.empty()) saved.startupScene = activeScene;
	// Открытие другого уровня не меняет стартовую сцену проекта; Ctrl+S пишет текущий документ.
	const auto path = (ProjectDiskPath(saved.Root()) / ProjectDiskPath(activeScene)).lexically_normal();
	std::error_code code;
	std::filesystem::create_directories(path.parent_path(), code);
	if (code) { error = "Cannot create scene directory: " + code.message(); return false; }
	const auto utf8 = path.generic_u8string();
	const std::string scenePath(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	// Манифест ссылается на сцену только после успешной атомарной записи самого уровня.
	if (!game.getLevel()->Save(scenePath, error) || !saved.Save(error)) return false;
	destination = std::move(saved);
	sceneFile = activeScene;
	savedScene = std::move(snapshot);
	sceneDirty = checkSceneDirty = false;
	return true;
}

void Editor::ContinuePendingAction()
{
	const PendingAction action = pendingAction;
	pendingAction = PendingAction::None;
	resumeAfterSave = false;
	if (action == PendingAction::Close) PostQuitMessage(0);
	else if (action == PendingAction::OpenProject) requestedProjectSwitch = true;
	else if (action == PendingAction::NewProject) { creationSavesCurrentScene = false; showNewProject = true; }
	else if (action == PendingAction::OpenObject) requestedObjectOpen = true;
	else if (action == PendingAction::ReturnScene) requestedSceneReturn = true;
	else if (action == PendingAction::OpenScene) requestedSceneOpen = true;
}

void Editor::ApplyDocumentActions()
{
	// Снимки снимаются между кадрами, после применения создания и удаления объектов.
	if (checkSceneDirty || checkPendingAction) RefreshSceneDirty();
	if (checkPendingAction) {
		checkPendingAction = false;
		if (game.getLevel() && sceneDirty) showUnsavedDialog = true;
		else ContinuePendingAction();
	}
	if (proceedPendingAction) { proceedPendingAction = false; ContinuePendingAction(); }
	if (!requestedSave) return;
	requestedSave = false;
	FinishTransformEdit();
	if (editedObject) {
		if (SaveObject(documentError) && resumeAfterSave) { closeUnsavedDialog = true; ContinuePendingAction(); }
		game.getTimeManager().reset();
		return;
	}
	if (temporaryProject || project.filePath.empty()) { creationSavesCurrentScene = true; showNewProject = true; return; }
	if (SaveScene(project, documentError)) {
		contentBrowser.RefreshFiles();
		if (resumeAfterSave) { closeUnsavedDialog = true; ContinuePendingAction(); }
	}
	game.getTimeManager().reset();
}

void Editor::ApplyProjectAction()
{
	const bool createDemo = initializeDemo;
	initializeDemo = false;
	if (requestedProjectCreation) {
		requestedProjectCreation = false;
		const Project* source = creationSavesCurrentScene && !project.filePath.empty() ? &project : nullptr;
		if (projectCreationTarget.filePath.empty()
			&& !Project::Create(newProjectParent, newProjectName, projectCreationTarget, projectCreationError, source)) return;
		if (!creationSavesCurrentScene) {
			// Новый уровень ещё не принадлежит Game: создаём и сохраняем демо без Init.
			auto* demo = new (std::nothrow) Level();
			GameObject* cameraObject = demo ? demo->createObject() : nullptr;
			auto* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
			auto* navigation = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
			if (!camera || !navigation || !PopulateDemoScene(*demo)) {
				delete demo; projectCreationError = "Cannot allocate the demo scene."; return;
			}
			cameraObject->getTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
			camera->bind_to_node(&cameraObject->getTransform());
			navigation->bind_to_node(&cameraObject->getTransform());
			Project ready = projectCreationTarget;
			ready.startupScene = ready.resourceDirectory + "/Scenes/Main.level.json";
			const auto path = (ProjectDiskPath(ready.Root()) / ProjectDiskPath(ready.startupScene)).generic_u8string();
			const std::string scenePath(reinterpret_cast<const char*>(path.data()), path.size());
			const bool saved = demo->Save(scenePath, projectCreationError) && ready.Save(projectCreationError);
			delete demo;
			game.getTimeManager().reset();
			if (!saved) return;
			pendingProject = std::move(ready);
			requestedProjectSwitch = true;
			projectCreationCompleted = true;
		} else {
			const bool complete = SaveScene(projectCreationTarget, projectCreationError);
			// Время копирования файлов не должно превращаться в большой шаг симуляции.
			game.getTimeManager().reset();
			if (!complete) return;
			// Сохранение текущей работы сохраняет объекты, выбор и историю Undo.
			game.getResourceManager().SetProjectDirectory(projectCreationTarget.Root());
			project = std::move(projectCreationTarget);
			temporaryProject = false;
			contentBrowser = ContentBrowser(project.ResourceRoot(), project.Root());
			recentProjects.Remember(project.filePath, projectError);
			projectCreationCompleted = true;
			if (resumeAfterSave) ContinuePendingAction();
			return;
		}
	}
	if (!requestedProjectSwitch) return;
	requestedProjectSwitch = false;
	// До передачи Game уровень и камера принадлежат этой операции.
	Level* level = nullptr;
	if (!pendingProject.startupScene.empty()) {
		rapidjson::Document json;
		const auto path = (ProjectDiskPath(pendingProject.Root()) / ProjectDiskPath(pendingProject.startupScene)).generic_u8string();
		const std::string scenePath(reinterpret_cast<const char*>(path.data()), path.size());
		if (!Deserialization::LoadFile(scenePath, json, projectError)) return;
		level = Level::Deserialize(json, game.getComponentFactoryMap(), projectError);
		if (!level) return;
	} else level = new (std::nothrow) Level();
	if (!level) { projectError = "Cannot allocate scene."; return; }
	GameObject* cameraObject = pendingProject.startupScene.empty() ? level->createObject() : nullptr;
	EditorCamera* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
	CameraMovementComponent* movement = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
	if (cameraObject && (!camera || !movement)) { delete level; projectError = "Cannot allocate a project camera."; return; }
	if (pendingProject.startupScene.empty() && !cameraObject) { delete level; projectError = "Cannot allocate a project camera."; return; }
	if (createDemo && !PopulateDemoScene(*level)) {
		delete level;
		projectError = "Cannot allocate the demonstration scene.";
		return;
	}
	if (cameraObject) {
		cameraObject->getTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
		camera->bind_to_node(&cameraObject->getTransform());
		movement->bind_to_node(&cameraObject->getTransform());
	}
	// Сбрасываем заимствованные адреса до Shutdown и удаления прежней сцены.
	ResetSceneReferences();
	sceneActionError.clear();
	if (!game.replaceLevel(level, pendingProject.Root())) {
		delete level;
		projectError = "Cannot initialize project systems. The previous project remains open.";
		return;
	}
	// После Init общий индекс содержит и компоненты загруженного дерева трансформаций.
	for (auto* object : level->getObjects())
		for (auto* component : object->getComponents()) {
			if (!camera) camera = dynamic_cast<EditorCamera*>(component);
			if (auto* navigation = dynamic_cast<CameraMovementComponent*>(component)) {
				navigation->enabled = false;
				navigation->rotationButton = Keys::MiddleButton;
			}
		}
	game.getRenderServer().setCamera(camera);
	game.getRenderServer().setAmbientColor({ 0.12f, 0.12f, 0.12f });
	project = std::move(pendingProject);
	sceneFile = project.startupScene;
	temporaryProject = createDemo;
	savedScene.clear();
	if (!project.startupScene.empty()) CaptureScene(savedScene, documentError);
	sceneDirty = savedScene.empty();
	contentBrowser = ContentBrowser(project.ResourceRoot(), project.Root());
	if (!temporaryProject) recentProjects.Remember(project.filePath, projectError);
}

const char* Editor::ObjectLabel(GameObject& object) const
{
	for (Component* component : object.getComponents()) {
		if (dynamic_cast<EditorCamera*>(component)) return "Camera";
		if (dynamic_cast<DirectionalLightComponent*>(component)) return "Directional light";
		if (dynamic_cast<PointLightComponent*>(component)) return "Point light";
		if (dynamic_cast<MeshComponent*>(component)) return "Mesh object";
	}
	return "Game object";
}

void Editor::DrawHierarchy()
{
	if (ImGui::Begin("Hierarchy", &showHierarchy)) {
		scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		Level* level = game.getLevel();
		ImGui::TextUnformatted(editedObject ? "Object asset" : "Scene");
		if (editedObject && ImGui::Button("Return to Scene")) { pendingAction = PendingAction::ReturnScene; checkPendingAction = true; }
		ImGui::SameLine();
		ImGui::BeginDisabled(level == nullptr || editedObject != nullptr);
		if (ImGui::SmallButton("+")) ImGui::OpenPopup("CreateObject");
		if (ImGui::BeginPopup("CreateObject")) {
			DrawCreateMenu();
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();
		if (!sceneActionError.empty()) ImGui::TextWrapped("%s", sceneActionError.c_str());
		ImGui::Separator();
		if (level) {
			int index = 0;
			for (GameObject* object : level->getObjects()) {
				if (!object || object->isPendingDestroy()) continue;
				if (editedObject && object != editedObject) continue;
				ImGui::PushID(object);
				const bool selected = selectedObject == object;
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
				if (selected) flags |= ImGuiTreeNodeFlags_Selected;
				const bool open = ImGui::TreeNodeEx("Object", flags, "%s %d", ObjectLabel(*object), ++index);
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
					selectedObject = object;
					selectedNode = &object->getTransform();
				}
				if (ImGui::BeginPopupContextItem("ObjectActions")) {
					selectedObject = object;
					selectedNode = &object->getTransform();
					if (ImGui::MenuItem("Duplicate", nullptr, false, editedObject == nullptr)) requestedDuplicate = object;
					if (ImGui::MenuItem("Delete", "Del", false, CanDeleteObject(object))) requestedDelete = object;
					if (!CanDeleteObject(object)) ImGui::TextDisabled("Active editor camera");
					ImGui::EndPopup();
				}
				if (open) {
					// Дерево трансформаций показано отдельно от списка компонентов.
					DrawTransformTree(object->getTransform(), 0);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
	}
	ImGui::End();
}

void Editor::DrawCreateMenu()
{
	if (ImGui::MenuItem("Empty object")) requestedCreate = ObjectType::Empty;
	if (ImGui::MenuItem("Mesh object")) requestedCreate = ObjectType::Mesh;
	if (ImGui::BeginMenu("3D primitives")) {
		if (ImGui::MenuItem("Cube")) requestedCreate = ObjectType::Cube;
		if (ImGui::MenuItem("Sphere")) requestedCreate = ObjectType::Sphere;
		if (ImGui::MenuItem("Plane")) requestedCreate = ObjectType::Plane;
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Lighting")) {
		if (ImGui::MenuItem("Directional light")) requestedCreate = ObjectType::DirectionalLight;
		if (ImGui::MenuItem("Point light")) requestedCreate = ObjectType::PointLight;
		ImGui::EndMenu();
	}
}

bool Editor::CanDeleteObject(GameObject* object) const
{
	if (editedObject) return false;
	if (!object || object->isPendingDestroy()) return false;
	const EditorCamera* camera = game.getRenderServer().getCamera();
	// Пока камера навигации принадлежит сцене, оставляем её доступной для работы.
	return !camera || camera->getOwner() != object;
}

void Editor::ApplySceneActions()
{
	Level* level = game.getLevel();
	const ObjectType type = requestedCreate;
	GameObject* removed = requestedDelete;
	GameObject* duplicated = requestedDuplicate;
	requestedCreate = ObjectType::None;
	requestedDelete = nullptr;
	requestedDuplicate = nullptr;
	if (!level || (type == ObjectType::None && !removed && !duplicated)) return;
	if (editedObject) return;
	FinishTransformEdit();
	sceneActionError.clear();
	if (CanDeleteObject(removed)) {
		meshPathDrafts.clear();
		level->destroyObject(removed);
		checkSceneDirty = true;
	}
	if (duplicated && !duplicated->isPendingDestroy()) {
		// Снимком владеет документ; новый объект передаём уровню только после полной сборки.
		rapidjson::Document document;
		auto json = duplicated->Serialize(document.GetAllocator(), sceneActionError);
		GameObject* copy = json.IsNull() ? nullptr : GameObject::Deserialize(json, game.getComponentFactoryMap(), sceneActionError);
		if (copy && !level->addObject(copy)) { delete copy; copy = nullptr; sceneActionError = "Cannot attach duplicated object."; }
		if (copy) { selectedObject = copy; selectedNode = &copy->getTransform(); checkSceneDirty = true; }
	}
	if (type == ObjectType::None) return;

	// Владение сразу принимает Level, редактор сохраняет только адрес выбора.
	GameObject* object = level->createObject();
	if (!object) {
		sceneActionError = "Cannot allocate scene object";
		return;
	}
	TransformableComponent* component = nullptr;
	switch (type) {
	case ObjectType::Mesh:
		component = object->createComponent<MeshComponent>(std::string{}, std::string{});
		break;
	case ObjectType::Cube: component = object->createComponent<MeshComponent>(); break;
	case ObjectType::Sphere:
		component = object->createComponent<MeshComponent>(MeshPresets::CreateCircle(0.5f));
		break;
	case ObjectType::Plane:
		component = object->createComponent<MeshComponent>(MeshPresets::CreateQuad(1.0f, 1.0f));
		object->getTransform().set_rotation(
			DXQuaternion::CreateFromAxisAngle(DXVector3::UnitX, -DirectX::XM_PIDIV2));
		break;
	case ObjectType::DirectionalLight: component = object->createComponent<DirectionalLightComponent>(); break;
	case ObjectType::PointLight: component = object->createComponent<PointLightComponent>(); break;
	case ObjectType::Empty: break;
	default: break;
	}
	if (type != ObjectType::Empty && !component) {
		// Не оставляем пустой объект вместо запрошенного примитива или света.
		level->destroyObject(object);
		sceneActionError = "Cannot allocate object component";
		return;
	}
	if (component) component->bind_to_node(&object->getTransform());
	object->getTransform().set_position(DXVector3::Zero);
	// Компоненты будут инициализированы общим applyPendingChanges в конце кадра.
	selectedObject = object;
	selectedNode = &object->getTransform();
	checkSceneDirty = true;
}

void Editor::ResetSceneReferences()
{
	ClearTransformHistory();
	selectedObject = nullptr; selectedNode = nameDraftNode = nullptr;
	meshPathDrafts.clear(); treeAction = {};
	requestedCreate = ObjectType::None; requestedDelete = requestedDuplicate = requestedObjectExport = nullptr;
	navigating = false;
	ImGui::ClearActiveID(); ImGui::ClearDragDrop();
}

bool Editor::ReplaceEditorLevel(Level* level, EditorCamera* camera, std::string& error)
{
	ResetSceneReferences();
	if (!game.replaceLevel(level, project.Root())) { error = "Cannot initialize document preview."; return false; }
	for (auto* object : level->getObjects()) for (auto* component : object->getComponents()) {
		if (!camera) camera = dynamic_cast<EditorCamera*>(component);
		if (auto* movement = dynamic_cast<CameraMovementComponent*>(component)) {
			movement->enabled = false; movement->rotationButton = Keys::MiddleButton;
		}
	}
	game.getRenderServer().setCamera(camera);
	game.getRenderServer().setAmbientColor({ 0.25f, 0.25f, 0.25f });
	return true;
}

void Editor::QueueObjectOpen(const std::string& file)
{
	pendingObjectFile = file;
	if (editedObject) { pendingAction = PendingAction::OpenObject; checkPendingAction = true; }
	else requestedObjectOpen = true;
}

void Editor::QueueSceneOpen(const std::string& file)
{
	const auto relative = ProjectDiskPath(file).lexically_normal().lexically_relative(ProjectDiskPath(project.Root()));
	if (relative.empty() || relative.has_root_path()) { documentError = "Choose a scene inside this project."; return; }
	for (const auto& part : relative) if (part == "..") { documentError = "Choose a scene inside this project."; return; }
	const auto path = relative.generic_u8string();
	pendingSceneFile.assign(reinterpret_cast<const char*>(path.data()), path.size());
	// Из режима объекта возвращаемся через оба независимых диалога сохранения.
	openSceneAfterReturn = editedObject != nullptr;
	pendingAction = editedObject ? PendingAction::ReturnScene : PendingAction::OpenScene;
	checkPendingAction = true;
}

bool Editor::SaveObject(std::string& error)
{
	std::string snapshot;
	if (!editedObject || !CaptureScene(snapshot, error) || !editedObject->Save(objectFile, error)) return false;
	savedScene = std::move(snapshot); sceneDirty = checkSceneDirty = false;
	return true;
}

void Editor::ApplyObjectActions()
{
	if (requestedObjectExport) {
		auto* object = requestedObjectExport; requestedObjectExport = nullptr;
		if (!object->isPendingDestroy() && object->Save(exportObjectPath, documentError)) {
			if (object == editedObject) { objectFile = exportObjectPath; CaptureScene(savedScene, documentError); RefreshSceneDirty(); }
			contentBrowser.RefreshFiles();
		}
	}
	if (requestedSceneReturn) {
		requestedSceneReturn = false;
		rapidjson::Document json;
		json.Parse(parkedScene.data(), parkedScene.size());
		Level* level = json.HasParseError() ? nullptr : Level::Deserialize(json, game.getComponentFactoryMap(), documentError);
		if (!level) { if (documentError.empty()) documentError = "Cannot restore the scene."; closeAfterReturn = false; return; }
		GameObject* previous = editedObject; editedObject = nullptr;
		if (!ReplaceEditorLevel(level, nullptr, documentError)) { editedObject = previous; delete level; closeAfterReturn = false; return; }
		savedScene = std::move(parkedBaseline); parkedScene.clear(); objectFile.clear();
		RefreshSceneDirty();
		if (closeAfterReturn) { closeAfterReturn = openSceneAfterReturn = false; RequestClose(); }
		else if (openSceneAfterReturn) { openSceneAfterReturn = false; pendingAction = PendingAction::OpenScene; checkPendingAction = true; }
	}
	if (requestedSceneOpen) {
		requestedSceneOpen = false;
		if (editedObject) { documentError = "Return to Scene before opening another level."; return; }
		rapidjson::Document json;
		const auto path = (ProjectDiskPath(project.Root()) / ProjectDiskPath(pendingSceneFile)).generic_u8string();
		const std::string file(reinterpret_cast<const char*>(path.data()), path.size());
		if (!Deserialization::LoadFile(file, json, documentError)) return;
		Level* level = Level::Deserialize(json, game.getComponentFactoryMap(), documentError);
		if (!level) return;
		// Старый документ живёт до полной загрузки нового и успешной подготовки систем.
		if (!ReplaceEditorLevel(level, nullptr, documentError)) { delete level; return; }
		sceneFile = pendingSceneFile;
		savedScene.clear(); CaptureScene(savedScene, documentError);
		sceneDirty = savedScene.empty(); checkSceneDirty = false;
	}
	if (!requestedObjectOpen) return;
	requestedObjectOpen = false;
	rapidjson::Document json;
	if (!Deserialization::LoadFile(pendingObjectFile, json, documentError)) return;
	GameObject* object = GameObject::Deserialize(json, game.getComponentFactoryMap(), documentError);
	if (!object) return;
	std::string backup;
	if (!editedObject && !CaptureScene(backup, documentError)) { delete object; return; }
	// До Init снимаем эталон ассета; камера и свет предпросмотра в него не входят.
	rapidjson::Document baselineDocument;
	auto description = object->Serialize(baselineDocument.GetAllocator(), documentError);
	rapidjson::StringBuffer bytes; rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);
	if (description.IsNull() || !description.Accept(writer)) { delete object; return; }
	const std::string baseline(bytes.GetString(), bytes.GetSize());
	auto* preview = new (std::nothrow) Level();
	if (!preview) { delete object; documentError = "Cannot allocate object preview."; return; }
	if (!preview->addObject(object)) { delete object; delete preview; documentError = "Cannot attach object to preview."; return; }
	GameObject* cameraObject = preview->createObject();
	auto* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
	auto* movement = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
	GameObject* lightObject = preview->createObject();
	auto* light = lightObject ? lightObject->createComponent<DirectionalLightComponent>() : nullptr;
	if (!camera || !movement || !light) { delete preview; documentError = "Cannot allocate preview camera or light."; return; }
	cameraObject->getTransform().set_position(object->getTransform().get_position() + DXVector3(0, 0, 3));
	lightObject->getTransform().set_rotation(DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0));
	light->intensity = 1.0f;
	const bool entering = editedObject == nullptr;
	const std::string oldBaseline = savedScene;
	GameObject* previous = editedObject; editedObject = object;
	if (!ReplaceEditorLevel(preview, camera, documentError)) { editedObject = previous; delete preview; return; }
	if (entering) { parkedScene = std::move(backup); parkedBaseline = oldBaseline; }
	objectFile = std::move(pendingObjectFile); savedScene = baseline;
	selectedObject = object; selectedNode = &object->getTransform();
	RefreshSceneDirty();
	game.getTimeManager().reset();
}

void Editor::ClearTransformHistory()
{
	FinishTransformEdit();
	history.clear(); historyPosition = 0;
	pendingEdit = {}; editingTransform = editingGizmo = false;
}

void Editor::ApplyTreeAction()
{
	TreeAction action = std::move(treeAction);
	treeAction = {};
	TransformNode* node = action.node;
	if (action.operation == TreeOperation::None || !node || node->isPendingDestroy()
		|| !node->getOwner() || node->getOwner()->isPendingDestroy()) return;
	FinishTransformEdit();
	sceneActionError.clear();
	bool changed = false;
	switch (action.operation) {
	case TreeOperation::Add:
		if (auto* child = node->add_child()) { selectedNode = child; selectedObject = node->getOwner(); changed = true; }
		else sceneActionError = "Cannot create a child branch.";
		break;
	case TreeOperation::Rename:
		changed = node->set_name(action.name);
		if (!changed) sceneActionError = "Branch name must be nonempty and unique among siblings.";
		break;
	case TreeOperation::Inheritance:
		node->set_inherit_position(action.inheritPosition);
		node->set_inherit_rotation(action.inheritRotation);
		node->set_inherit_scale(action.inheritScale);
		changed = true;
		break;
	case TreeOperation::Remove: {
		const auto* camera = game.getRenderServer().getCamera();
		const auto* movement = GetCameraMovement();
		if (!node->get_parent() || (camera && node->contains(camera->getTransformNode()))
			|| (movement && node->contains(movement->getTransformNode()))) {
			sceneActionError = "The root and branches containing the active editor camera cannot be deleted."; break;
		}
		// Выбор, черновики и история не должны переживать освобождение поддерева.
		selectedObject = node->getOwner(); selectedNode = node->get_parent();
		meshPathDrafts.clear(); nameDraftNode = nullptr;
		changed = node->get_parent()->remove_child(node);
		break;
	}
	case TreeOperation::Reparent: {
		auto* target = action.target;
		if (!target || !node->get_parent() || target->isPendingDestroy()
			|| target->getOwner() != node->getOwner() || node->contains(target)) {
			sceneActionError = "Choose a parent in the same object, outside the moved subtree."; break;
		}
		DXVector3 parentScale, parentPosition, scale, position;
		DXQuaternion parentRotation, rotation;
		if (!DXMatrix(target->worldMatrix()).Decompose(parentScale, parentRotation, parentPosition)) {
			sceneActionError = "Cannot decompose the parent transform."; break;
		}
		const DXMatrix parent = DXMatrix::CreateScale(node->is_inherit_scale() ? parentScale : DXVector3::One)
			* DXMatrix::CreateFromQuaternion(node->is_inherit_rotation() ? parentRotation : DXQuaternion::Identity)
			* DXMatrix::CreateTranslation(node->is_inherit_position() ? parentPosition : DXVector3::Zero);
		if (std::abs(parent.Determinant()) < 1e-8f) { sceneActionError = "The parent transform is singular."; break; }
		DXMatrix local = DXMatrix(node->worldMatrix()) * parent.Invert();
		if (!local.Decompose(scale, rotation, position)) { sceneActionError = "Cannot preserve the world pose."; break; }
		rotation.Normalize();
		const DXMatrix rebuilt = DXMatrix::CreateScale(scale) * DXMatrix::CreateFromQuaternion(rotation) * DXMatrix::CreateTranslation(position);
		bool representable = true;
		for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
			const float value = local.m[row][column];
			if (!std::isfinite(value) || std::abs(value - rebuilt.m[row][column]) > 0.0001f * (1.0f + std::abs(value))) representable = false;
		}
		// Матрицу со сдвигом осей нельзя незаметно заменить приближённым S/R/T.
		if (!representable || !node->reparent(*target)) { sceneActionError = "Cannot move branch: shear, duplicate name or invalid parent."; break; }
		node->set_position(position); node->set_rotation(rotation); node->set_scale(scale);
		changed = true;
		break;
	}
	case TreeOperation::MoveComponent: {
		auto* component = dynamic_cast<TransformableComponent*>(action.component);
		const bool rootOnly = dynamic_cast<BoxColliderComponent*>(component) || dynamic_cast<RigidBodyComponent*>(component);
		if (!component || !action.target || action.target->isPendingDestroy() || component->isPendingDestroy()
			|| component == game.getRenderServer().getCamera() || component == GetCameraMovement()
			|| (rootOnly && action.target->get_parent())) break;
		changed = node->move_component(component, *action.target);
		if (!changed) sceneActionError = "Cannot bind component to this branch.";
		break;
	}
	case TreeOperation::RemoveComponent:
		if (action.component && action.component != game.getRenderServer().getCamera() && action.component != GetCameraMovement()) {
			meshPathDrafts.clear(); node->getOwner()->destroyComponent(action.component); changed = true;
		}
		break;
	case TreeOperation::AddComponent: {
		Component* component = nullptr;
		const bool physics = action.kind == ComponentKind::Collider || action.kind == ComponentKind::StaticBody || action.kind == ComponentKind::DynamicBody;
		if (physics && node->get_parent()) { sceneActionError = "Physics components must use the root branch."; break; }
		if (action.kind == ComponentKind::StaticBody || action.kind == ComponentKind::DynamicBody) {
			bool hasBody = false;
			for (auto* existing : node->get_components()) if (!existing->isPendingDestroy() && dynamic_cast<RigidBodyComponent*>(existing)) hasBody = true;
			if (hasBody) { sceneActionError = "This object already has a rigid body."; break; }
		}
		switch (action.kind) {
		case ComponentKind::Mesh: component = node->create_component<MeshComponent>(); break;
		case ComponentKind::DirectionalLight: component = node->create_component<DirectionalLightComponent>(); break;
		case ComponentKind::PointLight: component = node->create_component<PointLightComponent>(); break;
		case ComponentKind::Camera: component = node->create_component<EditorCamera>(); break;
		case ComponentKind::Navigation: component = node->create_component<CameraMovementComponent>(); break;
		case ComponentKind::Collider: component = node->create_component<BoxColliderComponent>(); break;
		case ComponentKind::StaticBody: component = node->create_component<StaticRigidBodyComponent>(); break;
		case ComponentKind::DynamicBody: component = node->create_component<DynamicRigidBodyComponent>(); break;
		}
		changed = component != nullptr;
		if (!changed) sceneActionError = "Cannot allocate component.";
		break;
	}
	default: break;
	}
	if (changed) { ClearTransformHistory(); checkSceneDirty = true; nameDraftNode = nullptr; }
}

void Editor::DrawTreeInspector()
{
	if (nameDraftNode != selectedNode) { nameDraftNode = selectedNode; nodeNameDraft = selectedNode->get_name(); }
	std::vector<char> buffer((std::max)(size_t(256), nodeNameDraft.size() + 1), '\0');
	std::copy(nodeNameDraft.begin(), nodeNameDraft.end(), buffer.begin());
	const bool enter = ImGui::InputText("Branch name", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
	nodeNameDraft = buffer.data();
	if (enter || ImGui::IsItemDeactivatedAfterEdit() || (requestedSave && nodeNameDraft != selectedNode->get_name())) {
		if (selectedNode->set_name(nodeNameDraft)) { ClearTransformHistory(); checkSceneDirty = true; }
		else sceneActionError = "Branch name must be nonempty and unique among siblings.";
	}
	if (ImGui::Button("Add child branch")) treeAction = { TreeOperation::Add, selectedNode };
	ImGui::SameLine();
	const auto* camera = game.getRenderServer().getCamera();
	const bool protectedBranch = !selectedNode->get_parent() || (camera && selectedNode->contains(camera->getTransformNode()));
	ImGui::BeginDisabled(protectedBranch);
	if (ImGui::Button("Delete branch...")) ImGui::OpenPopup("Delete branch");
	ImGui::EndDisabled();
	if (ImGui::BeginPopupModal("Delete branch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("Delete this branch, its children and their components?");
		if (ImGui::Button("Delete")) { treeAction = { TreeOperation::Remove, selectedNode }; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	bool position = selectedNode->is_inherit_position(), rotation = selectedNode->is_inherit_rotation(), scale = selectedNode->is_inherit_scale();
	ImGui::SeparatorText("Inherit");
	// Короткие подписи не пересекаются по ID с полями локальной трансформации.
	ImGui::PushID("Inheritance");
	bool changed = ImGui::Checkbox("Position", &position);
	ImGui::SameLine();
	changed |= ImGui::Checkbox("Rotation", &rotation);
	ImGui::SameLine();
	changed |= ImGui::Checkbox("Scale", &scale);
	ImGui::PopID();
	if (changed) {
		treeAction = { TreeOperation::Inheritance, selectedNode };
		treeAction.inheritPosition = position; treeAction.inheritRotation = rotation; treeAction.inheritScale = scale;
	}
	if (!sceneActionError.empty()) ImGui::TextWrapped("%s", sceneActionError.c_str());
}

void Editor::DrawTransformTree(TransformNode& node, int index)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (selectedNode == &node) flags |= ImGuiTreeNodeFlags_Selected;
	if (node.get_children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
	const bool open = ImGui::TreeNodeEx(&node, flags, "%s", node.get_name().c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		selectedNode = &node;
		// Находим объект-владельца для узла, выбранного под другим объектом.
		TransformNode* root = &node;
		while (root->get_parent()) root = root->get_parent();
		for (GameObject* object : game.getLevel()->getObjects())
			if (object && &object->getTransform() == root) selectedObject = object;
	}
	if (node.get_parent() && ImGui::BeginDragDropSource()) {
		TransformNode* source = &node;
		ImGui::SetDragDropPayload("GG_TRANSFORM_NODE", &source, sizeof(source));
		ImGui::TextUnformatted(node.get_name().c_str()); ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget()) {
		if (const auto* payload = ImGui::AcceptDragDropPayload("GG_TRANSFORM_NODE"); payload && payload->DataSize == sizeof(TransformNode*))
			treeAction = { TreeOperation::Reparent, *static_cast<TransformNode* const*>(payload->Data), &node };
		ImGui::EndDragDropTarget();
	}
	if (open) {
		int childIndex = 0;
		for (TransformNode* child : node.get_children())
			if (child && !child->isPendingDestroy()) DrawTransformTree(*child, ++childIndex);
		ImGui::TreePop();
	}
}

void Editor::DrawInspector()
{
	if (ImGui::Begin("Inspector", &showInspector)) {
		scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!selectedObject || !selectedNode) ImGui::TextDisabled("Select an object in Hierarchy");
		else {
			ImGui::TextUnformatted(ObjectLabel(*selectedObject));
			DrawTreeInspector();
			ImGui::SeparatorText("Local transform");
			const TransformState before = CaptureTransform(selectedObject, selectedNode);
			DXVector3 position = selectedNode->get_position();
			DXVector3 scale = selectedNode->get_scale();
			if (ImGui::DragFloat3("Position", &position.x, 0.02f)) selectedNode->set_position(position);
			TrackTransformEdit(before);
			// Инспектор показывает градусы; TransformNode хранит кватернион.
			DXVector3 angles = selectedNode->get_rotation().ToEuler() * (180.0f / DirectX::XM_PI);
			if (ImGui::DragFloat3("Rotation", &angles.x, 0.5f)) {
				angles *= DirectX::XM_PI / 180.0f;
				selectedNode->set_rotation(DXQuaternion::CreateFromYawPitchRoll(angles.y, angles.x, angles.z));
			}
			TrackTransformEdit(before);
			if (ImGui::DragFloat3("Scale", &scale.x, 0.01f)) {
				// Не допускаем вырожденную матрицу при редактировании масштаба.
				for (int axis = 0; axis < 3; ++axis) {
					float& value = (&scale.x)[axis];
					if (std::abs(value) < 0.001f) value = std::copysign(0.001f, value);
				}
				selectedNode->set_scale(scale);
			}
			TrackTransformEdit(before);
			if (ImGui::Button("Reset transform")) {
				FinishTransformEdit();
				pendingEdit = before;
				editingTransform = true;
				selectedNode->set_position(DXVector3::Zero);
				selectedNode->set_rotation(DXQuaternion::Identity);
				selectedNode->set_scale(DXVector3::One);
				FinishTransformEdit();
			}
			ImGui::SeparatorText("Components");
			if (ImGui::Button("Add component")) ImGui::OpenPopup("Add component");
			if (ImGui::BeginPopup("Add component")) {
				const char* labels[] = { "Mesh", "Directional light", "Point light", "Camera", "Camera movement", "Box collider", "Static body", "Dynamic body" };
				for (int kind = 0; kind < 8; ++kind)
					if (ImGui::MenuItem(labels[kind], nullptr, false, kind < 5 || !selectedNode->get_parent())) {
						treeAction = { TreeOperation::AddComponent, selectedNode }; treeAction.kind = static_cast<ComponentKind>(kind);
					}
				ImGui::EndPopup();
			}
			for (Component* component : selectedObject->getComponents()) {
				if (!component || component->isPendingDestroy()) continue;
				auto* transformable = dynamic_cast<TransformableComponent*>(component);
				if (transformable ? transformable->getTransformNode() != selectedNode : selectedNode->get_parent() != nullptr) continue;
				ImGui::PushID(component);
				ImGui::Separator(); ImGui::TextUnformatted(component->GetSerializationType());
				const bool protectedComponent = component == game.getRenderServer().getCamera() || component == GetCameraMovement();
				ImGui::BeginDisabled(protectedComponent);
				if (ImGui::SmallButton("Remove component")) { treeAction = { TreeOperation::RemoveComponent, selectedNode }; treeAction.component = component; }
				if (transformable && ImGui::BeginCombo("Branch", NodePath(*selectedNode).c_str())) {
					std::vector<TransformNode*> nodes; CollectNodes(selectedObject->getTransform(), nodes);
					const bool rootOnly = dynamic_cast<BoxColliderComponent*>(component) || dynamic_cast<RigidBodyComponent*>(component);
					for (auto* target : nodes) {
						ImGui::PushID(target); ImGui::BeginDisabled(rootOnly && target->get_parent());
						if (ImGui::Selectable(NodePath(*target).c_str(), target == selectedNode)) {
							treeAction = { TreeOperation::MoveComponent, selectedNode, target }; treeAction.component = component;
						}
						ImGui::EndDisabled(); ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				ImGui::EndDisabled();
				if (auto* light = dynamic_cast<LightComponent*>(component)) {
					if (ImGui::Checkbox("Enabled", &light->enabled)) checkSceneDirty = true;
					if (ImGui::ColorEdit3("Color", &light->color.x)) checkSceneDirty = true;
					if (ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) checkSceneDirty = true;
					if (auto* point = dynamic_cast<PointLightComponent*>(light))
						if (ImGui::DragFloat("Range", &point->range, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) checkSceneDirty = true;
				} else if (dynamic_cast<EditorCamera*>(component)) ImGui::TextUnformatted("Camera");
				else if (dynamic_cast<CameraMovementComponent*>(component)) ImGui::TextUnformatted("Camera navigation");
				else if (auto* mesh = dynamic_cast<MeshComponent*>(component)) {
					DrawMeshInspector(*mesh);
				}
				else ImGui::TextUnformatted("Component");
				ImGui::PopID();
			}
		}
	}
	ImGui::End();
}

void Editor::DrawMeshInspector(MeshComponent& mesh)
{
	ImGui::TextUnformatted("Mesh renderer");
	auto [entry, inserted] = meshPathDrafts.try_emplace(&mesh);
	auto& draft = entry->second;
	// Смена путей из кода отражается в полях, не сбрасывая незавершённый ввод.
	if (inserted || draft.appliedMesh != mesh.getMeshPath())
		draft.mesh = draft.appliedMesh = mesh.getMeshPath();
	if (inserted || draft.appliedMaterial != mesh.getMaterialPath())
		draft.material = draft.appliedMaterial = mesh.getMaterialPath();

	auto drawPath = [&](bool isMesh, std::string& text, std::string& inputError) {
		ImGui::PushID(isMesh ? "MeshPath" : "MaterialPath");
		const std::string current = isMesh ? mesh.getMeshPath() : mesh.getMaterialPath();
		auto supported = [&](const std::string& path) {
			if (path.empty()) return true;
			std::string lower = path;
			for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if (!isMesh) return lower.ends_with(".material.json");
			const auto dot = lower.find_last_of('.');
			const auto slash = lower.find_last_of("/\\");
			return dot != std::string::npos && (slash == std::string::npos || dot > slash)
				&& MeshResource::SupportsExtension(lower.substr(dot));
		};
		auto apply = [&](std::string value) {
			// Завершаем ввод, чтобы ImGui не восстановил прежний текст после drop или отказа.
			if (ImGui::GetActiveID() == ImGui::GetID(isMesh ? "Mesh" : "Material"))
				ImGui::ClearActiveID();
			if (!supported(value)) {
				// Неподходящий формат не заменяет путь компонента или видимое значение.
				text = current;
				inputError = isMesh ? "Unsupported mesh format." : "Expected a .material.json file.";
				return;
			}
			checkSceneDirty = true;
			if (isMesh) mesh.setMeshPath(std::move(value));
			else mesh.setMaterialPath(std::move(value));
			text = isMesh ? mesh.getMeshPath() : mesh.getMaterialPath();
			(isMesh ? draft.appliedMesh : draft.appliedMaterial) = text;
			inputError.clear();
		};
		// Буфер покрывает Windows-пути; длинное перетаскиваемое значение не обрезается.
		std::vector<char> buffer((std::max)(size_t(32768), text.size() + 1), '\0');
		std::copy(text.begin(), text.end(), buffer.begin());
		const char* hint = isMesh ? (mesh.hasProceduralMesh() ? "Procedural geometry" : "Empty mesh")
			: "Default material";
		if (isMesh && mesh.getGeometryPreset() == MeshComponent::GeometryPreset::Cube) hint = "cube";
		if (isMesh && mesh.getGeometryPreset() == MeshComponent::GeometryPreset::Sphere) hint = "sphere";
		const bool enter = ImGui::InputTextWithHint(isMesh ? "Mesh" : "Material", hint,
			buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		text = buffer.data();
		if (enter || ImGui::IsItemDeactivatedAfterEdit() || (requestedSave && text != current)) apply(text);

		if (ImGui::BeginDragDropTarget()) {
			// Проверяем тип до Accept: неподходящий файл не подсвечивает поле как цель.
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();
			if (payload && payload->IsDataType("GG_RESOURCE_PATH") && payload->DataSize > 1) {
				const char* bytes = static_cast<const char*>(payload->Data);
				if (bytes[payload->DataSize - 1] == '\0') {
					const std::string value(bytes, payload->DataSize - 1);
					if (value.find('\0') == std::string::npos && supported(value))
						if (ImGui::AcceptDragDropPayload("GG_RESOURCE_PATH")) apply(value);
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear")) apply("");
		if (!inputError.empty()) ImGui::TextWrapped("%s", inputError.c_str());
		const auto& loadError = isMesh ? mesh.getMeshError() : mesh.getMaterialError();
		if (!loadError.empty()) {
			ImGui::TextWrapped("%s", loadError.c_str());
			ImGui::TextDisabled("%s", isMesh ? "Using an empty mesh" : "Using the default material");
		}
		ImGui::PopID();
	};
	drawPath(true, draft.mesh, draft.meshError);
	// Сбрасываем и применённый путь, и черновик, чтобы поле показало серую подсказку.
	auto selectPreset = [&](MeshComponent::GeometryPreset preset) {
		checkSceneDirty = true;
		mesh.setGeometryPreset(preset);
		draft.mesh.clear();
		draft.appliedMesh.clear();
		draft.meshError.clear();
	};
	if (ImGui::Button("Cube")) selectPreset(MeshComponent::GeometryPreset::Cube);
	ImGui::SameLine();
	if (ImGui::Button("Sphere")) selectPreset(MeshComponent::GeometryPreset::Sphere);
	drawPath(false, draft.material, draft.materialError);
}

void Editor::ReleaseViewport()
{
	if (colorView) colorView->Release();
	if (colorTarget) colorTarget->Release();
	if (colorTexture) colorTexture->Release();
	if (depthTarget) depthTarget->Release();
	if (depthTexture) depthTexture->Release();
	colorView = nullptr;
	colorTarget = nullptr;
	colorTexture = nullptr;
	depthTarget = nullptr;
	depthTexture = nullptr;
	viewport = {};
}

bool Editor::ResizeViewport(UINT width, UINT height)
{
	if (colorView && width == static_cast<UINT>(viewport.Width)
		&& height == static_cast<UINT>(viewport.Height)) return true;
	ReleaseViewport();
	if (!width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
		|| height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) return false;
	ID3D11Device* device = game.getRenderServer().Device();
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	HRESULT result = device->CreateTexture2D(&desc, nullptr, &colorTexture);
	if (SUCCEEDED(result)) result = device->CreateRenderTargetView(colorTexture, nullptr, &colorTarget);
	if (SUCCEEDED(result)) result = device->CreateShaderResourceView(colorTexture, nullptr, &colorView);
	// Любая ошибка освобождает весь частично созданный набор.
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (SUCCEEDED(result)) result = device->CreateTexture2D(&desc, nullptr, &depthTexture);
	if (SUCCEEDED(result)) result = device->CreateDepthStencilView(depthTexture, nullptr, &depthTarget);
	if (FAILED(result)) {
		ReleaseViewport();
		return false;
	}
	viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
	return true;
}

void Editor::DrawViewport()
{
	if (auto* movement = GetCameraMovement()) movement->enabled = false;
	if (ImGui::Begin("Viewport")) {
		scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (ImGui::RadioButton("Move", operation == ImGuizmo::TRANSLATE)) operation = ImGuizmo::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", operation == ImGuizmo::ROTATE)) operation = ImGuizmo::ROTATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", operation == ImGuizmo::SCALE)) operation = ImGuizmo::SCALE;
		ImGui::SameLine();
		bool local = mode == ImGuizmo::LOCAL;
		if (ImGui::Checkbox("Local", &local)) mode = local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
		ImGui::Checkbox("Snap", &useSnap);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		ImGui::DragFloat("Step", &snap, 0.1f, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::TextDisabled("MMB: look + WASD / Q E | RMB: menu | Ctrl+S: save | Ctrl+Z / Ctrl+Y: undo / redo");
		const ImVec2 size = ImGui::GetContentRegionAvail();
		if (size.x >= 1 && size.y >= 1 && ResizeViewport(static_cast<UINT>(size.x), static_cast<UINT>(size.y))) {
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(colorView)), size);
			// Обычные hover-проверки учитывают перекрывающие окна, popup и активные UI-элементы.
			const bool sceneHovered = ImGui::IsWindowHovered() && ImGui::IsItemHovered();
			const bool pickRequested = sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			// СКМ начинает навигацию только над изображением, без активного ввода или popup.
			const bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
			if (sceneHovered && !popup && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) navigating = true;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || popup || ImGui::GetIO().WantTextInput
				|| GetForegroundWindow() != game.getWindow().getWindow()) navigating = false;
			if (auto* movement = GetCameraMovement())
				movement->enabled = navigating && !ImGui::GetIO().WantTextInput && !ImGuizmo::IsUsing();
			if (navigating) checkSceneDirty = true;
			if (!ImGui::GetTopMostPopupModal()) {
				if (!editedObject) game.Update();
				else if (auto* movement = GetCameraMovement()) movement->Update(game, game.getTimeManager().get_deltaTime());
			}
			DrawGizmo(origin, size);
			// Manipulate сначала обновляет состояние gizmo для текущего положения мыши.
			if (pickRequested && !navigating && !ImGui::IsAnyItemActive()
				&& GetForegroundWindow() == game.getWindow().getWindow()
				&& !ImGui::IsMouseDown(ImGuiMouseButton_Middle)
				&& !(selectedNode && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))) PickObject(origin, size);
			if (sceneHovered && !navigating && !popup && !ImGui::IsAnyItemActive()
				&& !ImGuizmo::IsUsing() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				FinishTransformEdit();
				PickObject(origin, size);
				ImGui::OpenPopup("Viewport actions");
			}
			DrawSelectionBounds(origin, size);
			// Предыдущий проход ImGui мог оставить viewport SRV в t0.
			ID3D11ShaderResourceView* empty = nullptr;
			game.getRenderServer().Context()->PSSetShaderResources(0, 1, &empty);
			game.getRenderServer().DrawScene(colorTarget, depthTarget, viewport);
		} else {
			navigating = false;
			ImGui::TextDisabled("Viewport unavailable");
		}
	} else navigating = false;
	if (ImGui::BeginPopup("Viewport actions")) {
		if (selectedObject && !selectedObject->isPendingDestroy()) {
			if (ImGui::MenuItem("Duplicate object", nullptr, false, editedObject == nullptr)) requestedDuplicate = selectedObject;
			if (ImGui::MenuItem("Delete object", "Del", false, CanDeleteObject(selectedObject))) requestedDelete = selectedObject;
		} else if (ImGui::BeginMenu("Create object", game.getLevel() != nullptr && !editedObject)) {
			DrawCreateMenu(); ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
	ImGui::End();
}

void Editor::DrawSelectionBounds(const ImVec2& origin, const ImVec2& size)
{
	EditorCamera* camera = game.getRenderServer().getCamera();
	DXMatrix view;
	if (!selectedObject || selectedObject->isPendingDestroy() || !camera || !camera->getView(view)) return;
	const DXMatrix viewProjection = view * camera->getProjection(viewport.Width / viewport.Height);
	constexpr int edges[][2] = {
		{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
		{7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}
	};
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	for (Component* component : selectedObject->getComponents()) {
		auto* mesh = dynamic_cast<MeshComponent*>(component);
		if (!mesh || mesh->isPendingDestroy() || !mesh->getRenderItem()) continue;
		const auto& vertices = mesh->getMeshData().vertices;
		if (vertices.empty()) continue;
		DXVector3 minimum(vertices.front().position), maximum = minimum;
		for (const auto& vertex : vertices) {
			minimum = DXVector3::Min(minimum, DXVector3(vertex.position));
			maximum = DXVector3::Max(maximum, DXVector3(vertex.position));
		}
		// Локальные габариты поворачиваются и масштабируются вместе с компонентом.
		const TransformNode* node = mesh->getRenderItem()->transformNode;
		const DXMatrix matrix = (node ? DXMatrix(node->worldMatrix()) : DXMatrix::Identity) * viewProjection;
		Vector4 corners[8];
		for (int index = 0; index < 8; ++index)
			corners[index] = Vector4::Transform(Vector4(
				(index & 1) ? maximum.x : minimum.x, (index & 2) ? maximum.y : minimum.y,
				(index & 4) ? maximum.z : minimum.z, 1.0f), matrix);
		for (const auto& edge : edges) {
			Vector4 a = corners[edge[0]], b = corners[edge[1]];
			if (!ClipSelectionEdge(a, b)) continue;
			const ImVec2 first(origin.x + (a.x / a.w + 1) * size.x * 0.5f,
				origin.y + (1 - a.y / a.w) * size.y * 0.5f);
			const ImVec2 last(origin.x + (b.x / b.w + 1) * size.x * 0.5f,
				origin.y + (1 - b.y / b.w) * size.y * 0.5f);
			draw->AddLine(first, last, IM_COL32(255, 170, 45, 230), 1.5f);
		}
	}
	draw->PopClipRect();
}

void Editor::PickObject(const ImVec2& origin, const ImVec2& size)
{
	Level* level = game.getLevel();
	EditorCamera* camera = game.getRenderServer().getCamera();
	DXMatrix view;
	if (!level || !camera || !camera->getView(view)) return;
	// Координаты изображения переводим в пиксели реальной текстуры viewport.
	const ImVec2 mouse = ImGui::GetMousePos();
	const float x = (mouse.x - origin.x) * viewport.Width / size.x;
	const float y = (mouse.y - origin.y) * viewport.Height / size.y;
	const DXMatrix projection = camera->getProjection(viewport.Width / viewport.Height);
	const DXMatrix identity = DXMatrix::Identity;
	const DXVector3 rayOrigin = DirectX::XMVector3Unproject(DirectX::XMVectorSet(x, y, 0, 1),
		0, 0, viewport.Width, viewport.Height, 0, 1, projection, view, identity);
	const DXVector3 rayEnd = DirectX::XMVector3Unproject(DirectX::XMVectorSet(x, y, 1, 1),
		0, 0, viewport.Width, viewport.Height, 0, 1, projection, view, identity);
	DXVector3 direction = rayEnd - rayOrigin;
	float nearest = direction.Length();
	if (!std::isfinite(nearest) || nearest <= 0.0f) return;
	direction /= nearest;
	GameObject* hit = nullptr;

	for (GameObject* object : level->getObjects()) {
		if (!object || object->isPendingDestroy()) continue;
		if (editedObject && object != editedObject) continue;
		for (Component* component : object->getComponents()) {
			auto* mesh = dynamic_cast<MeshComponent*>(component);
			if (!mesh || mesh->isPendingDestroy()) continue;
			const RenderItem* item = mesh->getRenderItem();
			if (!item || !item->mesh || !item->mesh->IsValid() || !item->material
				|| !item->material->Shader() || !item->material->Shader()->IsValid()) continue;
			const auto& data = mesh->getMeshData();
			if (data.topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) continue;
			const DXMatrix world = item->transformNode ? DXMatrix(item->transformNode->worldMatrix()) : identity;
			const float determinant = world.Determinant();
			if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) continue;
			D3D11_RASTERIZER_DESC raster{};
			raster.CullMode = D3D11_CULL_BACK;
			const RenderState* state = item->material->State();
			if (state && state->Raster()) state->Raster()->GetDesc(&raster);

			// Мировые вершины позволяют сравнивать расстояния при любом масштабе объектов.
			for (size_t index = 0; index + 2 < data.indices.size(); index += 3) {
				const uint32_t a = data.indices[index], b = data.indices[index + 1], c = data.indices[index + 2];
				if (a >= data.vertices.size() || b >= data.vertices.size() || c >= data.vertices.size()) continue;
				const DXVector3 v0 = DXVector3::Transform(DXVector3(data.vertices[a].position), world);
				const DXVector3 v1 = DXVector3::Transform(DXVector3(data.vertices[b].position), world);
				const DXVector3 v2 = DXVector3::Transform(DXVector3(data.vertices[c].position), world);
				const float facing = (v1 - v0).Cross(v2 - v0).Dot(direction);
				const bool front = raster.FrontCounterClockwise ? facing < 0.0f : facing > 0.0f;
				if ((raster.CullMode == D3D11_CULL_BACK && !front)
					|| (raster.CullMode == D3D11_CULL_FRONT && front)) continue;
				float distance = 0;
				if (DirectX::TriangleTests::Intersects(rayOrigin, direction, v0, v1, v2, distance)
					&& distance >= 0.0f && distance < nearest) {
					nearest = distance;
					hit = object;
				}
			}
		}
	}
	FinishTransformEdit();
	selectedObject = hit;
	selectedNode = hit ? &hit->getTransform() : nullptr;
}

void Editor::DrawGizmo(const ImVec2& origin, const ImVec2& size)
{
	EditorCamera* camera = game.getRenderServer().getCamera();
	if (!selectedNode || !camera || navigating
		|| ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
	DXMatrix view;
	if (!camera->getView(view)) return;
	const DXMatrix projection = camera->getProjection(size.x / size.y);
	DXMatrix world = selectedNode->worldMatrix();
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
	const float steps[] = { snap, snap, snap };
	const TransformState before = CaptureTransform(selectedObject, selectedNode);
	const bool changed = ImGuizmo::Manipulate(&view._11, &projection._11, operation,
		operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : mode, &world._11,
		nullptr, useSnap ? steps : nullptr);
	if (ImGuizmo::IsUsing() && !editingGizmo) {
		FinishTransformEdit();
		pendingEdit = before;
		editingTransform = editingGizmo = true;
	}
	if (!ImGuizmo::IsUsing() && editingGizmo) FinishTransformEdit();
	if (!changed) return;
	DXMatrix parent;
	if (!ParentMatrix(*selectedNode, parent)) return;
	// Gizmo меняет мировую матрицу, узел хранит локальные значения.
	DXMatrix local = world * parent.Invert();
	DXVector3 scale, position;
	DXQuaternion rotation;
	if (!local.Decompose(scale, rotation, position)) return;
	if (std::abs(scale.x * scale.y * scale.z) < 1e-8f) return;
	rotation.Normalize();
	selectedNode->set_position(position);
	selectedNode->set_rotation(rotation);
	selectedNode->set_scale(scale);
}

Editor::TransformState Editor::CaptureTransform(GameObject* object, TransformNode* node) const
{
	return { object, node, node->get_position(), node->get_scale(), node->get_rotation() };
}

void Editor::TrackTransformEdit(const TransformState& before)
{
	// Один drag или ввод числа образует одну команду вместо команды на каждый кадр.
	if (ImGui::IsItemActivated() || (ImGui::IsItemActive() && !editingTransform)) {
		FinishTransformEdit();
		pendingEdit = before;
		editingTransform = true;
	}
	// После Ctrl+S продолжающийся drag начинает новую команду и снова помечает сцену.
	if (before.node && (before.position != before.node->get_position()
		|| before.rotation != before.node->get_rotation() || before.scale != before.node->get_scale())) checkSceneDirty = true;
	if (ImGui::IsItemDeactivated()) FinishTransformEdit();
}

void Editor::FinishTransformEdit()
{
	if (!editingTransform) return;
	editingTransform = editingGizmo = false;
	const TransformState after = CaptureTransform(pendingEdit.object, pendingEdit.node);
	if (pendingEdit.position == after.position && pendingEdit.rotation == after.rotation
		&& pendingEdit.scale == after.scale) return;
	// Новое изменение удаляет ветку Redo; история ограничена 128 жестами.
	history.erase(history.begin() + historyPosition, history.end());
	if (history.size() == 128) history.erase(history.begin());
	history.push_back({ pendingEdit, after });
	historyPosition = history.size();
	checkSceneDirty = true;
}

void Editor::ApplyTransform(const TransformState& state)
{
	state.node->set_position(state.position);
	state.node->set_rotation(state.rotation);
	state.node->set_scale(state.scale);
	selectedObject = state.object;
	selectedNode = state.node;
	checkSceneDirty = true;
}

void Editor::Undo()
{
	if (!editingTransform && historyPosition > 0) ApplyTransform(history[--historyPosition].before);
}

void Editor::Redo()
{
	if (!editingTransform && historyPosition < history.size()) ApplyTransform(history[historyPosition++].after);
}
