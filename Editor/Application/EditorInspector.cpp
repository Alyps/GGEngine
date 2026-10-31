#include "Editor.h"
#include "imgui_internal.h"
#include "RenderPresets.h"
#include "EditorFileUtils.h"
#include <cmath>
#include <limits>
using EditorAction = EditorSettings::Action;
namespace {
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

bool Editor::IsObjectSelected(GameObject* object) const
{
	const auto found = objectIds.find(object);
	return found != objectIds.end() && std::find(selectedObjects.begin(), selectedObjects.end(), found->second) != selectedObjects.end();
}

void Editor::SelectObject(GameObject* object, bool toggle)
{
	FinishTransformEdit(); FinishProperty();
	if (!object) {
		selectedObjects.clear(); selectedObject = nullptr; selectedNode = nameDraftNode = nullptr;
		return;
	}
	const HistoryId id = Id(object);
	const auto found = std::find(selectedObjects.begin(), selectedObjects.end(), id);
	if (toggle && found != selectedObjects.end()) {
		selectedObjects.erase(found);
		if (selectedObject != object) return;
		selectedObject = nullptr; selectedNode = nameDraftNode = nullptr;
		// После снятия активного объекта возвращаемся к последнему оставшемуся живому объекту.
		for (auto it = selectedObjects.rbegin(); it != selectedObjects.rend(); ++it) {
			const auto target = historyTargets.find(*it);
			if (target == historyTargets.end() || target->second.kind != TargetKind::Object) continue;
			auto* candidate = static_cast<GameObject*>(target->second.pointer);
			if (!candidate->isPendingDestroy()) { selectedObject = candidate; selectedNode = &candidate->getTransform(); break; }
		}
		return;
	}
	if (!toggle) selectedObjects.clear();
	selectedObjects.push_back(id);
	selectedObject = object; selectedNode = &object->getTransform(); nameDraftNode = nullptr;
}

void Editor::SelectNode(TransformNode* node)
{
	SelectObject(node ? node->getOwner() : nullptr);
	selectedNode = node;
}

std::vector<GameObject*> Editor::SelectedObjects() const
{
	std::vector<GameObject*> objects;
	for (HistoryId id : selectedObjects) {
		const auto found = historyTargets.find(id);
		if (found == historyTargets.end() || found->second.kind != TargetKind::Object) continue;
		auto* object = static_cast<GameObject*>(found->second.pointer);
		if (!object->isPendingDestroy()) objects.push_back(object);
	}
	return objects;
}

void Editor::SelectContextObject(GameObject* object)
{
	if (!object || !IsObjectSelected(object)) { SelectObject(object); return; }
	FinishTransformEdit(); FinishProperty();
	const HistoryId id = Id(object);
	selectedObjects.erase(std::find(selectedObjects.begin(), selectedObjects.end(), id));
	selectedObjects.push_back(id);
	selectedObject = object; selectedNode = &object->getTransform(); nameDraftNode = nullptr;
}

bool Editor::CanDeleteSelection() const
{
	const auto objects = SelectedObjects();
	return !objects.empty() && std::all_of(objects.begin(), objects.end(), [&](GameObject* object) { return CanDeleteObject(object); });
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
		const ImVec2 dropMinimum = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		if (level) {
			int index = 0;
			for (GameObject* object : level->getObjects()) {
				if (!object || object->isPendingDestroy()) continue;
				if (editedObject && object != editedObject) continue;
				ImGui::PushID(object);
				const bool selected = IsObjectSelected(object);
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
				if (selected) flags |= ImGuiTreeNodeFlags_Selected;
				const bool open = ImGui::TreeNodeEx("Object", flags, "%s %d", ObjectLabel(*object), ++index);
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
					SelectObject(object, settings.SelectionHeld());
				}
				if (ImGui::BeginPopupContextItem("ObjectActions")) {
					SelectContextObject(object);
					if (ImGui::MenuItem("Duplicate selection", settings.Label(EditorAction::Duplicate).c_str(), false, editedObject == nullptr)) requestedDuplicate = object;
					if (ImGui::MenuItem("Delete selection", settings.Label(EditorAction::Delete).c_str(), false, CanDeleteSelection())) requestedDelete = object;
					if (!CanDeleteSelection()) ImGui::TextDisabled("Selection includes the active editor camera");
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
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
			&& !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) SelectObject(nullptr);
		// Сброс на любую строку или свободное место создаёт объект уровня, без переподчинения.
		DrawObjectDropTarget(dropMinimum, ImVec2(dropMinimum.x + available.x, dropMinimum.y + available.y));
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
	const std::string inserted = std::move(requestedObjectInsert);
	requestedObjectInsert.clear();
	requestedCreate = ObjectType::None;
	requestedDelete = nullptr;
	requestedDuplicate = nullptr;
	if (!level || (type == ObjectType::None && !removed && !duplicated && inserted.empty())) return;
	if (editedObject) return;
	FinishTransformEdit();
	sceneActionError.clear();
	if (!inserted.empty()) {
		rapidjson::Document json;
		const auto file = EditorFiles::Utf8Path(EditorFiles::DiskPath(project.Root()) / EditorFiles::DiskPath(inserted));
		if (!Deserialization::LoadFile(file, json, sceneActionError)) return;
		GameObject* object = GameObject::Deserialize(json, game.getComponentFactoryMap(), sceneActionError);
		if (!object) return;
		object->getTransform().set_position(objectInsertPosition);
		// До addObject экземпляром владеет эта операция, после успеха — только Level.
		if (!level->addObject(object)) {
			delete object; sceneActionError = "Cannot attach object to scene."; return;
		}
		BeginStructure(object, "Insert object", true);
		SelectObject(object);
		checkSceneDirty = true;
	}
	if (removed && CanDeleteSelection()) {
		const auto targets = IsObjectSelected(removed) ? SelectedObjects() : std::vector<GameObject*>{ removed };
		const size_t firstRecord = pendingStructures.size();
		for (auto* target : targets) {
			if (!CanDeleteObject(target) || !BeginStructure(target, "Delete selection")) {
				pendingStructures.resize(firstRecord); return;
			}
		}
		for (auto* target : targets) level->destroyObject(target);
		meshPathDrafts.clear(); checkSceneDirty = true;
	}
	if (duplicated && !duplicated->isPendingDestroy()) {
		const auto sources = IsObjectSelected(duplicated) ? SelectedObjects() : std::vector<GameObject*>{ duplicated };
		std::vector<GameObject*> copies;
		// Все копии полностью собираются до передачи владения уровню.
		for (auto* source : sources) {
			rapidjson::Document document;
			auto json = source->Serialize(document.GetAllocator(), sceneActionError);
			auto* copy = json.IsNull() ? nullptr : GameObject::Deserialize(json, game.getComponentFactoryMap(), sceneActionError);
			if (!copy) { for (auto* pending : copies) delete pending; return; }
			copies.push_back(copy);
		}
		for (size_t i = 0; i < copies.size(); ++i) {
			if (level->addObject(copies[i])) continue;
			for (size_t j = 0; j < i; ++j) level->destroyObject(copies[j]);
			for (size_t j = i; j < copies.size(); ++j) delete copies[j];
			sceneActionError = "Cannot attach duplicated objects."; return;
		}
		for (auto* copy : copies) BeginStructure(copy, "Duplicate selection", true);
		for (size_t i = 0; i < copies.size(); ++i) SelectObject(copies[i], i != 0);
		checkSceneDirty = true;
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
	BeginStructure(object, "Create object", true);
	// Компоненты будут инициализированы общим applyPendingChanges в конце кадра.
	SelectObject(object);
	checkSceneDirty = true;
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
	FinishProperty();
	const bool structural = action.operation == TreeOperation::Add || action.operation == TreeOperation::Remove
		|| action.operation == TreeOperation::AddComponent || action.operation == TreeOperation::RemoveComponent
		|| action.operation == TreeOperation::Reparent || action.operation == TreeOperation::MoveComponent;
	const char* label = action.operation == TreeOperation::Add ? "Add branch"
		: action.operation == TreeOperation::Remove ? "Delete branch"
		: action.operation == TreeOperation::AddComponent ? "Add component"
		: action.operation == TreeOperation::RemoveComponent ? "Delete component"
		: action.operation == TreeOperation::Reparent ? "Reparent branch"
		: action.operation == TreeOperation::MoveComponent ? "Bind component" : "Inheritance";
	if (structural && !BeginStructure(node->getOwner(), label)) return;
	HistoryEntry property; property.name = label;
	property.kind = HistoryKind::Node; property.target = Id(node);
	if (!structural) property.before = NodeProperties(*node);
	bool changed = false;
	switch (action.operation) {
	case TreeOperation::Add:
		if (auto* child = node->add_child()) { SelectNode(child); changed = true; }
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
		// Выбор и черновики очищаются до освобождения поддерева; история хранит его описание.
		SelectNode(node->get_parent());
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
		if (!IsValidEditorScale(scale)) { sceneActionError = "Scale must be positive and at least 0.001 on every axis."; break; }
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
	if (changed) {
		if (!structural) {
			property.after = NodeProperties(*node);
			PushHistory(std::move(property));
		}
		checkSceneDirty = true; nameDraftNode = nullptr;
	}
}

void Editor::DrawTreeInspector()
{
	if (nameDraftNode != selectedNode) { nameDraftNode = selectedNode; nodeNameDraft = selectedNode->get_name(); }
	std::vector<char> buffer((std::max)(size_t(256), nodeNameDraft.size() + 1), '\0');
	std::copy(nodeNameDraft.begin(), nodeNameDraft.end(), buffer.begin());
	const bool enter = ImGui::InputText("Branch name", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
	nodeNameDraft = buffer.data();
	if (enter || ImGui::IsItemDeactivatedAfterEdit() || (requestedSave && nodeNameDraft != selectedNode->get_name())) {
		BeginProperty(HistoryKind::Node, Id(selectedNode), NodeProperties(*selectedNode), "Rename branch");
		if (selectedNode->set_name(nodeNameDraft)) checkSceneDirty = true;
		else sceneActionError = "Branch name must be nonempty and unique among siblings.";
		FinishProperty();
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
		SelectNode(&node);
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

void Editor::DrawTransformField(TransformField field, const char* label, bool fromGroupCenter)
{
	const auto before = CaptureTransform(selectedObject, selectedNode);
	const DXVector3 pivot = fromGroupCenter ? SelectionCenter() : before.position;
	DXVector3 value = field == TransformField::Position ? pivot : field == TransformField::Scale ? before.scale
		: before.rotation.ToEuler() * (180.0f / DirectX::XM_PI);
	const float speed = field == TransformField::Position ? 0.02f : field == TransformField::Rotation ? 0.5f : 0.01f;
	const bool scaling = field == TransformField::Scale;
	if (ImGui::DragFloat3(label, &value.x, speed, scaling ? MinimumScale : 0.0f,
		scaling ? (std::numeric_limits<float>::max)() : 0.0f, "%.3f", scaling ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None)) {
		if (field == TransformField::Position) selectedNode->set_position(fromGroupCenter ? before.position + value - pivot : value);
		else if (field == TransformField::Rotation) {
			// UI вводит градусы; узел хранит кватернион с прежним порядком Euler-осей.
			value *= DirectX::XM_PI / 180.0f;
			selectedNode->set_rotation(DXQuaternion::CreateFromYawPitchRoll(value.y, value.x, value.z));
		} else {
			for (int axis = 0; axis < 3; ++axis) {
				float& scale = (&value.x)[axis];
				scale = std::isfinite(scale) ? (std::max)(MinimumScale, scale) : MinimumScale;
			}
			selectedNode->set_scale(value);
		}
	}
	// Одиночный и групповой ввод завершаются одной командой существующей истории.
	TrackTransformEdit(before, fromGroupCenter);
}

void Editor::DrawInspector()
{
	if (ImGui::Begin("Inspector", &showInspector)) {
		scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!selectedObject || !selectedNode) ImGui::TextDisabled("Select an object in Hierarchy");
		else {
			if (selectedObjects.size() > 1) ImGui::TextDisabled("%zu selected | Group transform", selectedObjects.size());
			ImGui::TextUnformatted(ObjectLabel(*selectedObject));
			DrawTreeInspector();
			ImGui::SeparatorText("Local transform");
			DrawTransformField(TransformField::Position, "Position");
			DrawTransformField(TransformField::Rotation, "Rotation");
			DrawTransformField(TransformField::Scale, "Scale");
			if (ImGui::Button("Reset transform")) {
				FinishTransformEdit(); FinishProperty();
				const auto before = CaptureTransform(selectedObject, selectedNode);
				pendingEdit = before;
				CaptureTransformGroup(before);
				editingTransform = true;
				selectedNode->set_position(DXVector3::Zero);
				selectedNode->set_rotation(DXQuaternion::Identity);
				selectedNode->set_scale(DXVector3::One);
				if (!pendingGroup.empty() && !ApplyTransformGroupStep(before)) {
					selectedNode->set_position(before.position); selectedNode->set_rotation(before.rotation); selectedNode->set_scale(before.scale);
				}
				FinishTransformEdit();
			}
			ImGui::SeparatorText("Components");
			if (selectedObjects.size() > 1) ImGui::TextDisabled("Component properties: active object");
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
					// Снимок берётся до виджета; непрерывный ввод остаётся одной командой.
					auto before = ComponentProperties(*light);
					bool changed = ImGui::Checkbox("Enabled", &light->enabled);
					TrackComponentProperty(*light, before, "Light enabled", changed);
					before = ComponentProperties(*light);
					changed = ImGui::ColorEdit3("Color", &light->color.x);
					TrackComponentProperty(*light, before, "Light color", changed);
					before = ComponentProperties(*light);
					changed = ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					TrackComponentProperty(*light, before, "Light intensity", changed);
					if (auto* point = dynamic_cast<PointLightComponent*>(light)) {
						before = ComponentProperties(*light);
						changed = ImGui::DragFloat("Range", &point->range, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
						TrackComponentProperty(*light, before, "Light range", changed);
					}
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
			BeginProperty(HistoryKind::Component, Id(&mesh), ComponentProperties(mesh), isMesh ? "Mesh source" : "Material");
			if (isMesh) mesh.setMeshPath(std::move(value));
			else mesh.setMaterialPath(std::move(value));
			FinishProperty();
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
		BeginProperty(HistoryKind::Component, Id(&mesh), ComponentProperties(mesh), "Mesh primitive");
		mesh.setGeometryPreset(preset);
		FinishProperty();
		draft.mesh.clear();
		draft.appliedMesh.clear();
		draft.meshError.clear();
	};
	if (ImGui::Button("Cube")) selectPreset(MeshComponent::GeometryPreset::Cube);
	ImGui::SameLine();
	if (ImGui::Button("Sphere")) selectPreset(MeshComponent::GeometryPreset::Sphere);
	drawPath(false, draft.material, draft.materialError);
}
