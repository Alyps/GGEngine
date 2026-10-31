#include "Editor.h"
#include "imgui_internal.h"
#include "EditorFileUtils.h"
#include <cmath>
#include <DirectXCollision.h>
using DirectX::SimpleMath::Vector4;
using EditorAction = EditorSettings::Action;
namespace {
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

void Editor::DrawViewportToolbar()
{
	const auto& style = ImGui::GetStyle();
	const float width = ImGui::GetContentRegionAvail().x;
	const float start = ImGui::GetCursorPosX();
	const float gap = style.ItemSpacing.x;
	const auto buttonWidth = [&](const char* text) { return ImGui::CalcTextSize(text).x + style.FramePadding.x * 2; };
	const float playbackWidth = buttonWidth("Play") + buttonWidth("Pause") + buttonWidth("Stop") + gap * 2;
	const float toolsWidth = buttonWidth("Move") + buttonWidth("Rotate") + buttonWidth("Scale") + gap * 2;
	const float optionsWidth = buttonWidth("...");
	const bool compact = width < toolsWidth + playbackWidth + optionsWidth + gap * 4;
	const auto toolButton = [&](const char* label, ImGuizmo::OPERATION tool) {
		const bool selected = operation == tool;
		if (selected) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
		if (ImGui::Button(label)) { FinishTransformEdit(); operation = tool; }
		if (selected) ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) {
			const auto action = tool == ImGuizmo::TRANSLATE ? EditorAction::Move : tool == ImGuizmo::ROTATE ? EditorAction::Rotate : EditorAction::Scale;
			ImGui::SetTooltip("%s (%s)", label, settings.Label(action).c_str());
		}
	};
	const auto drawOptions = [&]() {
		if (ImGui::Button("...")) ImGui::OpenPopup("Viewport options");
		if (ImGui::BeginPopup("Viewport options")) {
			bool local = mode == ImGuizmo::LOCAL;
			if (ImGui::Checkbox("Local", &local)) mode = local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
			ImGui::Checkbox("Snap", &useSnap);
			ImGui::SetNextItemWidth(100);
			ImGui::DragFloat("Step", &snap, 0.1f, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndPopup();
		}
	};
	// В узкой панели инструменты сворачиваются, оставляя место кнопкам запуска.
	ImGui::BeginGroup();
	if (compact) {
		if (ImGui::Button("Tools")) ImGui::OpenPopup("Viewport tools");
		if (ImGui::BeginPopup("Viewport tools")) {
			if (ImGui::MenuItem("Move", settings.Label(EditorAction::Move).c_str(), operation == ImGuizmo::TRANSLATE)) operation = ImGuizmo::TRANSLATE;
			if (ImGui::MenuItem("Rotate", settings.Label(EditorAction::Rotate).c_str(), operation == ImGuizmo::ROTATE)) operation = ImGuizmo::ROTATE;
			if (ImGui::MenuItem("Scale", settings.Label(EditorAction::Scale).c_str(), operation == ImGuizmo::SCALE)) operation = ImGuizmo::SCALE;
			ImGui::EndPopup();
		}
	} else {
		toolButton("Move", ImGuizmo::TRANSLATE); ImGui::SameLine();
		toolButton("Rotate", ImGuizmo::ROTATE); ImGui::SameLine();
		toolButton("Scale", ImGuizmo::SCALE);
	}
	ImGui::EndGroup();
	const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
	// Центрируем запуск относительно viewport; на очень малой ширине выделяем ему строку.
	const bool separateRow = width < buttonWidth("Tools") + playbackWidth + optionsWidth + gap * 4;
	if (!separateRow) ImGui::SameLine((std::max)(leftEnd + gap, start + (width - playbackWidth) * 0.5f));
	else {
		ImGui::SameLine((std::max)(leftEnd + gap, start + width - optionsWidth));
		drawOptions();
		ImGui::SetCursorPosX(start + (std::max)(0.0f, (width - playbackWidth) * 0.5f));
	}
	// Точки подключения будущего игрового режима; пока кнопки не меняют состояние сцены.
	ImGui::Button("Play");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start game (not connected yet)");
	ImGui::SameLine(); ImGui::Button("Pause");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause game (not connected yet)");
	ImGui::SameLine(); ImGui::Button("Stop");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop game (not connected yet)");
	if (!separateRow) {
		ImGui::SameLine(start + width - optionsWidth); drawOptions();
	}
	ImGui::Separator();
}

bool Editor::DrawSelectionTransformPanel(const ImVec2& origin, const ImVec2& size)
{
	if (selectedObjects.size() < 2 || !selectedObject || !selectedNode || selectedNode->get_parent()
		|| size.x < 220 || size.y < 60) return false;
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const float width = (std::min)(320.0f, size.x - 16.0f);
	ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - width - 8, origin.y + 8));
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(width, size.y - 16));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	ImGui::BeginChild("Selection transform panel", ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
	scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (ImGui::CollapsingHeader("Selection transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextDisabled("%zu objects | %s", selectedObjects.size(), groupFromCenter ? "Shared center" : "Active object");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Position follows the selected pivot. Rotation and scale use the active object's values and transform the whole group around that pivot.");
		ImGui::BeginDisabled(navigating || editingGizmo);
		// Общий pivot мини-панели и гизмо; инспектор использует активный объект.
		if (ImGui::Checkbox("From group center", &groupFromCenter)) { FinishTransformEdit(); FinishProperty(); }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pivot for this panel and gizmo: selection center when checked, active object when unchecked.");
		if (ImGui::BeginTable("Transform values", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Rotation").x);
			ImGui::TableSetupColumn("XYZ", ImGuiTableColumnFlags_WidthStretch);
			const char* labels[] = { "Position", "Rotation", "Scale" };
			for (int field = 0; field < 3; ++field) {
				ImGui::PushID(field); ImGui::TableNextRow(); ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(labels[field]); ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				DrawTransformField(static_cast<TransformField>(field), "##XYZ", groupFromCenter);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
		if (!sceneActionError.empty()) ImGui::TextWrapped("%s", sceneActionError.c_str());
	}
	// Весь прямоугольник панели перехватывает мышь, включая фон и свёрнутый заголовок.
	const ImVec2 panelPosition = ImGui::GetWindowPos(), panelSize = ImGui::GetWindowSize();
	const bool hovered = ImGui::IsMouseHoveringRect(panelPosition, ImVec2(panelPosition.x + panelSize.x, panelPosition.y + panelSize.y), false);
	ImGui::EndChild(); ImGui::PopStyleColor();
	ImGui::SetCursorScreenPos(cursor);
	return hovered;
}

void Editor::DrawViewport()
{
	if (auto* movement = GetCameraMovement()) movement->enabled = false;
	if (ImGui::Begin("Viewport")) {
		scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		DrawViewportToolbar();
		const auto selectionHints = settings.ViewportHints(false), cameraHints = settings.ViewportHints(true);
		const auto& shortcuts = navigating ? cameraHints : selectionHints;
		ImVec2 size = ImGui::GetContentRegionAvail();
		// Полоска занимает место вне изображения: на ней нельзя выбрать объект или вращать камеру.
		// Высота остаётся постоянной при переключении подсказок, чтобы не пересоздавать render target.
		const float footerHeight = (std::max)(ImGui::CalcTextSize(selectionHints.c_str(), nullptr, false, (std::max)(1.0f, size.x)).y,
			ImGui::CalcTextSize(cameraHints.c_str(), nullptr, false, (std::max)(1.0f, size.x)).y)
			+ ImGui::GetStyle().ItemSpacing.y * 2 + 1.0f;
		size.y = (std::max)(0.0f, size.y - footerHeight);
		const float footerY = ImGui::GetCursorPosY() + size.y + ImGui::GetStyle().ItemSpacing.y;
		if (size.x >= 1 && size.y >= 1 && ResizeViewport(static_cast<UINT>(size.x), static_cast<UINT>(size.y))) {
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(colorView)), size);
			// Обычные hover-проверки учитывают перекрывающие окна, popup и активные UI-элементы.
			const bool imageHovered = ImGui::IsWindowHovered() && ImGui::IsItemHovered();
			const bool panelHovered = DrawSelectionTransformPanel(origin, size);
			const bool sceneHovered = imageHovered && !panelHovered;
			if (!panelHovered && !navigating && !ImGuizmo::IsUsing())
				DrawObjectDropTarget(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
			const bool pickRequested = sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			// Назначенная кнопка начинает навигацию только над изображением, без активного UI.
			const bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
			if (sceneHovered && !popup && !ImGui::IsAnyItemActive() && settings.Pressed(EditorAction::Navigate)) navigating = true;
			if (!settings.Held(EditorAction::Navigate) || popup || ImGui::GetIO().WantTextInput
				|| GetForegroundWindow() != game.getWindow().getWindow()) navigating = false;
			if (auto* movement = GetCameraMovement()) {
				movement->rotationButton = NavigationButton();
				const auto modifiers = settings.Get(EditorAction::Navigate).modifiers;
				// Компонент получает направление по значению, без зависимости движка от ImGui.
				movement->setMovementInput(DXVector3(
					float(settings.Held(EditorAction::Right, modifiers)) - float(settings.Held(EditorAction::Left, modifiers)),
					float(settings.Held(EditorAction::Up, modifiers)) - float(settings.Held(EditorAction::Down, modifiers)),
					float(settings.Held(EditorAction::Backward, modifiers)) - float(settings.Held(EditorAction::Forward, modifiers))));
				movement->enabled = navigating && !ImGui::GetIO().WantTextInput && !ImGuizmo::IsUsing();
			}
			if (navigating) checkSceneDirty = true;
			if (!ImGui::GetTopMostPopupModal()) {
				if (!editedObject) game.Update();
				else if (auto* movement = GetCameraMovement()) movement->Update(game, game.getTimeManager().get_deltaTime());
			}
			DrawGizmo(origin, size, panelHovered);
			// Manipulate сначала обновляет состояние gizmo для текущего положения мыши.
			if (pickRequested && !navigating && !ImGui::IsAnyItemActive()
				&& GetForegroundWindow() == game.getWindow().getWindow()
				&& !settings.Held(EditorAction::Navigate)
				&& (settings.SelectionHeld() || !(selectedNode && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))))
				PickObject(origin, size, settings.SelectionHeld());
			if (sceneHovered && !navigating && !popup && !ImGui::IsAnyItemActive()
				&& !ImGuizmo::IsUsing() && settings.Pressed(EditorAction::ContextMenu)) {
				FinishTransformEdit();
				PickObject(origin, size, false, true);
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
		ImGui::SetCursorPosY(footerY);
		ImGui::Separator();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(shortcuts.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	} else navigating = false;
	if (ImGui::BeginPopup("Viewport actions")) {
		if (selectedObject && !selectedObject->isPendingDestroy()) {
			if (ImGui::MenuItem("Duplicate selection", settings.Label(EditorAction::Duplicate).c_str(), false, editedObject == nullptr)) requestedDuplicate = selectedObject;
			if (ImGui::MenuItem("Delete selection", settings.Label(EditorAction::Delete).c_str(), false, CanDeleteSelection())) requestedDelete = selectedObject;
		} else if (ImGui::BeginMenu("Create object", game.getLevel() != nullptr && !editedObject)) {
			DrawCreateMenu(); ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
	ImGui::End();
}

void Editor::DrawObjectDropTarget(const ImVec2& minimum, const ImVec2& maximum, bool inViewport)
{
	if (!game.getLevel() || editedObject || maximum.x <= minimum.x || maximum.y <= minimum.y) return;
	const auto* payload = ImGui::GetDragDropPayload();
	if (!payload || !payload->IsDataType("GG_RESOURCE_PATH") || payload->DataSize <= 1) return;
	const char* bytes = static_cast<const char*>(payload->Data);
	if (bytes[payload->DataSize - 1] != '\0') return;
	const std::string path(bytes, payload->DataSize - 1);
	std::string lower = path;
	for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	if (!lower.ends_with(".object.json") || path.find('\0') != std::string::npos) return;
	// Контент-браузер передаёт только пути внутри проекта, не абсолютные имена файлов.
	const auto relative = EditorFiles::DiskPath(path);
	if (relative.has_root_path()) return;
	for (const auto& part : relative) if (part == "..") return;
	if (!ImGui::BeginDragDropTargetCustom(ImRect(minimum, maximum), ImGui::GetID("Object asset drop"))) return;
	if (ImGui::AcceptDragDropPayload("GG_RESOURCE_PATH")) {
		DXVector3 position = DXVector3::Zero;
		if (inViewport) {
			EditorCamera* camera = game.getRenderServer().getCamera();
			DXMatrix view;
			if (!camera || !camera->getView(view)) { ImGui::EndDragDropTarget(); return; }
			const float width = maximum.x - minimum.x, height = maximum.y - minimum.y;
			const ImVec2 mouse = ImGui::GetMousePos();
			const DXMatrix projection = camera->getProjection(width / height);
			const DXVector3 start = DirectX::XMVector3Unproject(DirectX::XMVectorSet(mouse.x - minimum.x, mouse.y - minimum.y, 0, 1),
				0, 0, width, height, 0, 1, projection, view, DXMatrix::Identity);
			const DXVector3 end = DirectX::XMVector3Unproject(DirectX::XMVectorSet(mouse.x - minimum.x, mouse.y - minimum.y, 1, 1),
				0, 0, width, height, 0, 1, projection, view, DXMatrix::Identity);
			DXVector3 direction = end - start;
			const float length = direction.Length();
			if (!std::isfinite(length) || length <= 0) { ImGui::EndDragDropTarget(); return; }
			direction /= length;
			// При взгляде вдоль земли или в небо ставим экземпляр перед камерой.
			const float ground = std::abs(direction.y) > 1e-5f ? -start.y / direction.y : -1.0f;
			position = start + direction * (std::isfinite(ground) && ground >= 0 && ground <= 10000 ? ground : 5.0f);
			if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
				ImGui::EndDragDropTarget(); return;
			}
		}
		// Изменение уровня откладывается до окончания всех обходов и отрисовки кадра.
		requestedObjectInsert = path;
		objectInsertPosition = position;
	}
	ImGui::EndDragDropTarget();
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
	for (auto* object : game.getLevel()->getObjects()) {
		if (!object || object->isPendingDestroy() || !IsObjectSelected(object)) continue;
		const ImU32 color = object == selectedObject ? IM_COL32(255, 170, 45, 230) : IM_COL32(100, 185, 255, 230);
		for (Component* component : object->getComponents()) {
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
				draw->AddLine(first, last, color, 1.5f);
			}
		}
	}
	draw->PopClipRect();
}

void Editor::PickObject(const ImVec2& origin, const ImVec2& size, bool toggle, bool context)
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
	if (context) SelectContextObject(hit); else SelectObject(hit, toggle);
}

void Editor::DrawGizmo(const ImVec2& origin, const ImVec2& size, bool blocked)
{
	EditorCamera* camera = game.getRenderServer().getCamera();
	if (!selectedNode || !camera || navigating || (blocked && !editingGizmo)
		|| (settings.SelectionHeld() && !editingGizmo)
		|| ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
	DXMatrix view;
	if (!camera->getView(view)) return;
	const DXMatrix projection = camera->getProjection(size.x / size.y);
	DXMatrix world = selectedNode->worldMatrix();
	const bool group = selectedObjects.size() > 1 && !selectedNode->get_parent();
	if (group) {
		if (!editingGizmo) {
			const DXVector3 center = groupFromCenter ? SelectionCenter() : selectedNode->get_position();
			groupGizmoStart = DXMatrix::CreateFromQuaternion(selectedNode->get_rotation()) * DXMatrix::CreateTranslation(center);
			groupGizmoCurrent = groupGizmoStart;
		}
		world = groupGizmoCurrent;
	}
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
	const float steps[] = { snap, snap, snap };
	const TransformState before = CaptureTransform(selectedObject, selectedNode);
	const bool changed = ImGuizmo::Manipulate(&view._11, &projection._11, operation,
		operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : mode, &world._11,
		nullptr, useSnap ? steps : nullptr);
	if (ImGuizmo::IsUsing() && !editingGizmo) {
		FinishTransformEdit(); FinishProperty();
		pendingEdit = before;
		CaptureTransformGroup(before);
		editingTransform = editingGizmo = true;
	}
	if (!ImGuizmo::IsUsing() && editingGizmo) FinishTransformEdit();
	if (!changed) return;
	if (group) {
		if (ApplyTransformGroup(groupGizmoStart.Invert() * world)) groupGizmoCurrent = world;
		return;
	}
	DXMatrix parent;
	if (!ParentMatrix(*selectedNode, parent)) return;
	// Gizmo меняет мировую матрицу, узел хранит локальные значения.
	DXMatrix local = world * parent.Invert();
	DXVector3 scale, position;
	DXQuaternion rotation;
	if (!local.Decompose(scale, rotation, position)) return;
	if (!IsValidEditorScale(scale)) return;
	rotation.Normalize();
	selectedNode->set_position(position);
	selectedNode->set_rotation(rotation);
	selectedNode->set_scale(scale);
}
