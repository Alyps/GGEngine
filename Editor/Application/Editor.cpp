#include "Editor.h"
#include "imgui_internal.h"
#include "EditorFileUtils.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using EditorAction = EditorSettings::Action;
using EditorFiles::DiskPath;
using EditorFiles::PickObjectSavePath;
using EditorFiles::PickProjectPath;
namespace { constexpr wchar_t EditorProperty[] = L"GGEngine.Editor"; }

Editor::Editor(Game& application)
	: game(application), contentBrowser("")
{
	game.setEditorMode(true);
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

void Editor::ConfigureCameraNavigation(CameraMovementComponent& movement) const
{
	movement.enabled = false;
	movement.rotationButton = NavigationButton();
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
	if (auto* movement = GetCameraMovement()) { movement->enabled = true; movement->useDefaultMovementInput(); }
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
	// Клик без движения включает ввод числа; перетаскивание с ЛКМ меняет значение.
	io.ConfigDragClickToInputText = true;
	// Пока нет проекта сцены, не создаём imgui.ini в исходниках.
	io.IniFilename = nullptr;
	ImGui::StyleColorsDark();
	settings.Load(settingsError);
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
		// Авторская сцена не симулируется: её состояние меняют команды редактора.
		if (!game.isEditorMode() && !ImGui::GetTopMostPopupModal() && !game.StepPhysics()) return 1;
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
		scenePanelFocused = false;
		saveShortcut = false;
		// Назначенное сохранение доступно и в полях инспектора до записи сцены.
		if (!navigating && !settings.Held(EditorAction::Navigate) && GetForegroundWindow() == window
			&& !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
			saveShortcut = settings.Shortcut(EditorAction::Save);
		if (saveShortcut) requestedSave = true;
		DrawDockspace();
		DrawProjectDialogs();
		DrawSettings();
		if (showHierarchy) DrawHierarchy();
		if (showInspector) DrawInspector();
		DrawViewport();
		contentBrowser.Draw(showContentBrowser);
		if (const auto file = contentBrowser.TakeObjectToOpen(); !file.empty()) QueueObjectOpen(file);
		if (const auto file = contentBrowser.TakeSceneToOpen(); !file.empty()) QueueSceneOpen(file);
		// Закрываем жест также при смене выбора или скрытии панели.
		if (editingTransform && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing()) FinishTransformEdit();
		if (editingProperty && !ImGui::IsAnyItemActive()
			&& !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) FinishProperty();
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
			// Выбор заимствует адреса; история хранит ID и переживает освобождение объектов.
			for (GameObject* object : level->getObjects()) {
				if (!object) continue;
				// Удалённый компонент не должен оставлять черновик для повторно занятого адреса.
				for (Component* component : object->getComponents())
					if (component && component->isPendingDestroy())
						if (auto* mesh = dynamic_cast<MeshComponent*>(component)) meshPathDrafts.erase(mesh);
				if (!object->isPendingDestroy()) continue;
				if (IsObjectSelected(object)) SelectObject(object, true);
				meshPathDrafts.clear();
				nameDraftNode = nullptr;
				if (selectedObject == object) {
					selectedObject = nullptr;
					selectedNode = nullptr;
				}
				editingTransform = editingGizmo = false;
				pendingEdit = {};
				pendingGroup.clear();
			}
		}
		ForgetDeadHistoryTargets();
		game.EndFrame();
		FinishStructures();
		ApplyHistoryStep();
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
			if (ImGui::MenuItem(editedObject ? "Save Object" : "Save Scene", settings.Label(EditorAction::Save).c_str(), false, game.getLevel() != nullptr)) requestedSave = true;
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
				const auto folder = DiskPath(project.Root()).make_preferred();
				if (reinterpret_cast<INT_PTR>(ShellExecuteW(game.getWindow().getWindow(), L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
					projectError = "Cannot open project folder.";
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) RequestClose();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			const auto& entries = documentHistory.entries;
			const size_t position = documentHistory.position;
			const std::string undo = position ? "Undo: " + entries[position - 1].name : "Undo";
			const std::string redo = position < entries.size() ? "Redo: " + entries[position].name : "Redo";
			if (ImGui::MenuItem(undo.c_str(), settings.Label(EditorAction::Undo).c_str(), false, !editingTransform && !editingProperty && position > 0)) Undo();
			if (ImGui::MenuItem(redo.c_str(), settings.Label(EditorAction::Redo).c_str(), false, !editingTransform && !editingProperty && position < entries.size())) Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Editor settings...")) openSettings = true;
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

Keys Editor::NavigationButton() const
{
	return settings.Get(EditorAction::Navigate).key == ImGuiKey_MouseRight ? Keys::RightButton : Keys::MiddleButton;
}

void Editor::DrawSettings()
{
	if (openSettings) {
		openSettings = false; settingsDraft = settings; capturingBinding = -1;
		FinishTransformEdit(); FinishProperty();
		ImGui::OpenPopup("Editor settings");
	}
	ImGui::SetNextWindowSize(ImVec2(640, 540), ImGuiCond_FirstUseEver);
	if (!ImGui::BeginPopupModal("Editor settings", nullptr, ImGuiWindowFlags_NoDocking)) return;
	ImGui::TextUnformatted("Click a binding, then press the new key combination.");
	bool beginCapture = false;
	ImGui::BeginChild("Bindings", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 4));
	if (ImGui::BeginTable("Shortcuts", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Action"); ImGui::TableSetupColumn("Binding"); ImGui::TableHeadersRow();
		for (size_t i = 0; i < EditorSettings::ActionCount; ++i) {
			const auto action = static_cast<EditorAction>(i);
			ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow(); ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(EditorSettings::Name(action));
			ImGui::TableNextColumn();
			if (ImGui::Button(settingsDraft.Label(action).c_str(), ImVec2(-1, 0))) {
				capturingBinding = static_cast<int>(i); bindingCaptureFrame = ImGui::GetFrameCount(); beginCapture = true;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	const char* modifier = settingsDraft.selectionModifier == ImGuiMod_Ctrl ? "Ctrl" : settingsDraft.selectionModifier == ImGuiMod_Shift ? "Shift" : "Alt";
	if (ImGui::BeginCombo("Multi-selection modifier", modifier)) {
		if (ImGui::Selectable("Ctrl", settingsDraft.selectionModifier == ImGuiMod_Ctrl)) settingsDraft.selectionModifier = ImGuiMod_Ctrl;
		if (ImGui::Selectable("Shift", settingsDraft.selectionModifier == ImGuiMod_Shift)) settingsDraft.selectionModifier = ImGuiMod_Shift;
		if (ImGui::Selectable("Alt", settingsDraft.selectionModifier == ImGuiMod_Alt)) settingsDraft.selectionModifier = ImGuiMod_Alt;
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("Camera directions are active while holding the navigation binding.");
	ImGui::EndChild();
	if (!settingsError.empty()) ImGui::TextWrapped("%s", settingsError.c_str());
	if (ImGui::Button("Save and apply")) {
		// Применяем только после проверки конфликтов и успешной записи файла.
		if (settingsDraft.Save(settingsError)) { settings = settingsDraft; navigating = false; ImGui::CloseCurrentPopup(); }
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset defaults")) { settingsDraft = EditorSettings(); settingsError.clear(); }
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) { capturingBinding = -1; ImGui::CloseCurrentPopup(); }
	if (beginCapture) ImGui::OpenPopup("Assign binding");
	if (ImGui::BeginPopupModal("Assign binding", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		const auto action = static_cast<EditorAction>(capturingBinding);
		ImGui::TextUnformatted(EditorSettings::Name(action));
		ImGui::TextUnformatted(action == EditorAction::Navigate || action == EditorAction::ContextMenu
			? "Press MMB or RMB, optionally with modifiers. Escape cancels." : "Press a key with optional modifiers. Escape cancels.");
		// Клик, открывший захват, не становится новым назначением.
		if (ImGui::GetFrameCount() > bindingCaptureFrame + 1) {
			for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
				if (!EditorSettings::AcceptsKey(action, static_cast<ImGuiKey>(key)) || !ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) continue;
				settingsDraft.Get(action) = { static_cast<ImGuiKey>(key), ImGui::GetIO().KeyMods };
				settingsError.clear(); capturingBinding = -1; ImGui::CloseCurrentPopup(); break;
			}
		}
		if (ImGui::Button("Cancel binding") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { capturingBinding = -1; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	ImGui::EndPopup();
}

void Editor::HandleShortcuts()
{
	// Не регистрируем Z/Y глобально во время ввода: ими пользуется текстовый редактор ImGui.
	if (GetForegroundWindow() != game.getWindow().getWindow() || navigating || ImGuizmo::IsUsing()
		|| ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()
		|| ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
	if (!scenePanelFocused || contentBrowser.IsFocused()) return;
	if (settings.Shortcut(EditorAction::Undo)) Undo();
	if (settings.Shortcut(EditorAction::Redo)) Redo();
	if (settings.Shortcut(EditorAction::Duplicate) && !editedObject && selectedObject) requestedDuplicate = selectedObject;
	if (settings.Shortcut(EditorAction::Move)) { FinishTransformEdit(); operation = ImGuizmo::TRANSLATE; }
	if (settings.Shortcut(EditorAction::Rotate)) { FinishTransformEdit(); operation = ImGuizmo::ROTATE; }
	if (settings.Shortcut(EditorAction::Scale)) { FinishTransformEdit(); operation = ImGuizmo::SCALE; }
	if (settings.Shortcut(EditorAction::Delete)) {
		if (selectedNode && selectedNode->get_parent()) treeAction = { TreeOperation::Remove, selectedNode };
		else if (CanDeleteSelection()) requestedDelete = selectedObject;
	}
}
