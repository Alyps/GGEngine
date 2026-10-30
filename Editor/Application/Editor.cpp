#include "Editor.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "RenderPresets.h"
#include <cmath>
#include <DirectXCollision.h>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using DirectX::SimpleMath::Vector4;

namespace {
	constexpr wchar_t EditorProperty[] = L"GGEngine.Editor";

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

Editor::Editor(Game& application, const std::string& resourceRoot)
	: game(application), contentBrowser(resourceRoot) {}

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
		if (!game.StepPhysics()) return 1;
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
		DrawDockspace();
		if (showHierarchy) DrawHierarchy();
		if (showInspector) DrawInspector();
		DrawViewport();
		contentBrowser.Draw(showContentBrowser);
		// Закрываем жест также при смене выбора или скрытии панели.
		if (editingTransform && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing()) FinishTransformEdit();
		// Общие сочетания регистрируются каждый кадр независимо от выбранной панели.
		// Без RouteGlobal Shortcut привязывается к неактивному служебному окну ImGui.
		const bool undoPressed = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal);
		const bool redoPressed = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal);
		const bool deletePressed = ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal);
		if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()
			&& !ImGuizmo::IsUsing() && !navigating && !contentBrowser.IsFocused()) {
			if (undoPressed) Undo();
			if (redoPressed) Redo();
			if (deletePressed && CanDeleteObject(selectedObject)) requestedDelete = selectedObject;
		}
		ImGui::Render();
		game.getRenderServer().BeginMainPass();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		game.getRenderServer().Present();
		// Структурные операции выполняются после обходов и рисования текущего кадра.
		ApplySceneActions();
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
	}
}

void Editor::DrawDockspace()
{
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
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
		Level* level = game.getLevel();
		ImGui::TextUnformatted("Scene");
		ImGui::SameLine();
		ImGui::BeginDisabled(level == nullptr);
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
	requestedCreate = ObjectType::None;
	requestedDelete = nullptr;
	if (!level || (type == ObjectType::None && !removed)) return;
	FinishTransformEdit();
	sceneActionError.clear();
	if (CanDeleteObject(removed)) {
		meshPathDrafts.clear();
		level->destroyObject(removed);
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
}

void Editor::DrawTransformTree(TransformNode& node, int index)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (selectedNode == &node) flags |= ImGuiTreeNodeFlags_Selected;
	if (node.get_children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
	const bool open = ImGui::TreeNodeEx(&node, flags, index ? "Transform %d" : "Root transform", index);
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		selectedNode = &node;
		// Находим объект-владельца для узла, выбранного под другим объектом.
		TransformNode* root = &node;
		while (root->get_parent()) root = root->get_parent();
		for (GameObject* object : game.getLevel()->getObjects())
			if (object && &object->getTransform() == root) selectedObject = object;
	}
	if (open) {
		int childIndex = 0;
		for (TransformNode* child : node.get_children())
			if (child) DrawTransformTree(*child, ++childIndex);
		ImGui::TreePop();
	}
}

void Editor::DrawInspector()
{
	if (ImGui::Begin("Inspector", &showInspector)) {
		if (!selectedObject || !selectedNode) ImGui::TextDisabled("Select an object in Hierarchy");
		else {
			ImGui::TextUnformatted(ObjectLabel(*selectedObject));
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
			for (Component* component : selectedObject->getComponents()) {
				ImGui::PushID(component);
				if (auto* light = dynamic_cast<LightComponent*>(component)) {
					ImGui::Checkbox("Enabled", &light->enabled);
					ImGui::ColorEdit3("Color", &light->color.x);
					ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					if (auto* point = dynamic_cast<PointLightComponent*>(light))
						ImGui::DragFloat("Range", &point->range, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
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
		if (enter || ImGui::IsItemDeactivatedAfterEdit()) apply(text);

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
		ImGui::TextDisabled("Hold RMB: WASD / Q E + mouse | Changes are not saved to disk");
		const ImVec2 size = ImGui::GetContentRegionAvail();
		if (size.x >= 1 && size.y >= 1 && ResizeViewport(static_cast<UINT>(size.x), static_cast<UINT>(size.y))) {
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(colorView)), size);
			// Обычные hover-проверки учитывают перекрывающие окна, popup и активные UI-элементы.
			const bool sceneHovered = ImGui::IsWindowHovered() && ImGui::IsItemHovered();
			const bool pickRequested = sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			// Навигация начинается только над изображением и заканчивается при отпускании ПКМ.
			if (sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) navigating = true;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)
				|| GetForegroundWindow() != game.getWindow().getWindow()) navigating = false;
			if (auto* movement = GetCameraMovement())
				movement->enabled = navigating && !ImGui::GetIO().WantTextInput && !ImGuizmo::IsUsing();
			game.Update();
			DrawGizmo(origin, size);
			// Manipulate сначала обновляет состояние gizmo для текущего положения мыши.
			if (pickRequested && !navigating && !ImGui::IsAnyItemActive()
				&& GetForegroundWindow() == game.getWindow().getWindow()
				&& !ImGui::IsMouseDown(ImGuiMouseButton_Right)
				&& !(selectedNode && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))) PickObject(origin, size);
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
	if (!selectedNode || !camera || navigating) return;
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
	if (ImGui::IsItemActivated()) {
		FinishTransformEdit();
		pendingEdit = before;
		editingTransform = true;
	}
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
}

void Editor::ApplyTransform(const TransformState& state)
{
	state.node->set_position(state.position);
	state.node->set_rotation(state.rotation);
	state.node->set_scale(state.scale);
	selectedObject = state.object;
	selectedNode = state.node;
}

void Editor::Undo()
{
	if (!editingTransform && historyPosition > 0) ApplyTransform(history[--historyPosition].before);
}

void Editor::Redo()
{
	if (!editingTransform && historyPosition < history.size()) ApplyTransform(history[historyPosition++].after);
}
