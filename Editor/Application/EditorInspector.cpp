#include "Editor.h"
#include "imgui_internal.h"
#include "RenderPresets.h"
#include "EditorFileUtils.h"
#include <cmath>
#include <cstdio>
#include <limits>
using EditorAction = EditorSettings::Action;
namespace {
	const char* TextureFormatName(DXGI_FORMAT format)
	{
		// ImGui использует Texture2D<float4>; целочисленные, depth и массивы не подменяем цветом.
		switch (format) {
		case DXGI_FORMAT_R8_UNORM: return "R8 UNORM";
		case DXGI_FORMAT_A8_UNORM: return "A8 UNORM";
		case DXGI_FORMAT_R8G8_UNORM: return "RG8 UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8 UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8 sRGB";
		case DXGI_FORMAT_B8G8R8A8_UNORM: return "BGRA8 UNORM";
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "BGRA8 sRGB";
		case DXGI_FORMAT_B8G8R8X8_UNORM: return "BGRX8 UNORM";
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return "BGRX8 sRGB";
		case DXGI_FORMAT_B5G6R5_UNORM: return "BGR 565";
		case DXGI_FORMAT_B5G5R5A1_UNORM: return "BGRA 5551";
		case DXGI_FORMAT_B4G4R4A4_UNORM: return "BGRA 4444";
		case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10 A2 UNORM";
		case DXGI_FORMAT_R11G11B10_FLOAT: return "RGB 11/11/10 FLOAT";
		case DXGI_FORMAT_R16_UNORM: return "R16 UNORM";
		case DXGI_FORMAT_R16_FLOAT: return "R16 FLOAT";
		case DXGI_FORMAT_R16G16_UNORM: return "RG16 UNORM";
		case DXGI_FORMAT_R16G16_FLOAT: return "RG16 FLOAT";
		case DXGI_FORMAT_R16G16B16A16_UNORM: return "RGBA16 UNORM";
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16 FLOAT";
		case DXGI_FORMAT_R32_FLOAT: return "R32 FLOAT";
		case DXGI_FORMAT_R32G32_FLOAT: return "RG32 FLOAT";
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32 FLOAT";
		case DXGI_FORMAT_BC1_UNORM: return "BC1 UNORM";
		case DXGI_FORMAT_BC1_UNORM_SRGB: return "BC1 sRGB";
		case DXGI_FORMAT_BC2_UNORM: return "BC2 UNORM";
		case DXGI_FORMAT_BC2_UNORM_SRGB: return "BC2 sRGB";
		case DXGI_FORMAT_BC3_UNORM: return "BC3 UNORM";
		case DXGI_FORMAT_BC3_UNORM_SRGB: return "BC3 sRGB";
		case DXGI_FORMAT_BC4_UNORM: return "BC4 UNORM";
		case DXGI_FORMAT_BC4_SNORM: return "BC4 SNORM";
		case DXGI_FORMAT_BC5_UNORM: return "BC5 UNORM";
		case DXGI_FORMAT_BC5_SNORM: return "BC5 SNORM";
		case DXGI_FORMAT_BC6H_UF16: return "BC6H UF16";
		case DXGI_FORMAT_BC6H_SF16: return "BC6H SF16";
		case DXGI_FORMAT_BC7_UNORM: return "BC7 UNORM";
		case DXGI_FORMAT_BC7_UNORM_SRGB: return "BC7 sRGB";
		default: return nullptr;
		}
	}

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
		if (dynamic_cast<EditorCamera*>(component)) return "Editor camera";
		if (dynamic_cast<GameCamera*>(component)) return "GameCamera";
		if (dynamic_cast<DirectionalLightComponent*>(component)) return "Directional light";
		if (dynamic_cast<PointLightComponent*>(component)) return "Point light";
		if (dynamic_cast<NavMeshVolumeComponent*>(component)) return "Nav mesh volume";
		if (dynamic_cast<CharacterMovementComponent*>(component)) return "Character";
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
			for (GameObject* object : level->getObjects()) {
				if (!object || object->isPendingDestroy()) continue;
				// Служебная камера остаётся во владении уровня, но не входит в авторскую иерархию.
				const auto* camera = game.getRenderServer().getCamera();
				if (camera && camera->getOwner() == object) continue;
				if (editedObject && object != editedObject) continue;
				DrawTransformTree(object->getTransform());
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
	if (ImGui::BeginMenu("Navigation")) {
		if (ImGui::MenuItem("Nav mesh volume")) requestedCreate = ObjectType::NavMeshVolume;
		ImGui::EndMenu();
	}
}

bool Editor::CanDeleteObject(GameObject* object) const
{
	if (editedObject) return false;
	if (!object || object->isPendingDestroy()) return false;
	const CameraComponent* camera = game.getRenderServer().getCamera();
	// Пока камера навигации принадлежит сцене, оставляем её доступной для работы.
	return !camera || camera->getOwner() != object;
}

void Editor::ApplySceneActions()
{
	Level* level = game.getLevel();
	const ObjectType type = requestedCreate;
	GameObject* removed = requestedDelete;
	GameObject* duplicated = requestedDuplicate;
	const ObjectInsertRequest inserted = std::move(requestedObjectInsert);
	requestedObjectInsert = {};
	requestedCreate = ObjectType::None;
	requestedDelete = nullptr;
	requestedDuplicate = nullptr;
	if (!level || (type == ObjectType::None && !removed && !duplicated && inserted.path.empty())) return;
	if (editedObject) return;
	FinishTransformEdit();
	sceneActionError.clear();
	if (!inserted.path.empty()) {
		rapidjson::Document json;
		const auto file = EditorFiles::Utf8Path(EditorFiles::DiskPath(project.Root()) / EditorFiles::DiskPath(inserted.path));
		if (!Deserialization::LoadFile(file, json, sceneActionError)) return;
		GameObject* object = GameObject::Deserialize(json, game.getComponentFactoryMap(), sceneActionError);
		if (!object) return;
		object->getTransform().set_position(inserted.position);
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
	case ObjectType::NavMeshVolume: component = object->createComponent<NavMeshVolumeComponent>(); break;
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
		const bool rootOnly = dynamic_cast<BoxColliderComponent*>(component) || dynamic_cast<RigidBodyComponent*>(component)
			|| dynamic_cast<CharacterMovementComponent*>(component) || dynamic_cast<NavAgentComponent*>(component);
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
		// Виды начиная с Collider работают только на корневой ветке.
		if (action.kind >= ComponentKind::Collider && node->get_parent()) { sceneActionError = "This component must use the root branch."; break; }
		// Тело и персонаж управляют одной позой корня, поэтому на объекте допустим только один из них.
		if (action.kind == ComponentKind::StaticBody || action.kind == ComponentKind::DynamicBody
			|| action.kind == ComponentKind::KinematicBody || action.kind == ComponentKind::CharacterMovement) {
			bool hasBody = false;
			for (auto* existing : node->get_components())
				if (!existing->isPendingDestroy() && (dynamic_cast<RigidBodyComponent*>(existing) || dynamic_cast<CharacterMovementComponent*>(existing))) hasBody = true;
			if (hasBody) { sceneActionError = "This object already has a rigid body or character."; break; }
		}
		if (action.kind == ComponentKind::NavAgent) {
			bool hasAgent = false;
			for (auto* existing : node->get_components()) if (!existing->isPendingDestroy() && dynamic_cast<NavAgentComponent*>(existing)) hasAgent = true;
			if (hasAgent) { sceneActionError = "This object already has a nav agent."; break; }
		}
		switch (action.kind) {
		case ComponentKind::Mesh: component = node->create_component<MeshComponent>(); break;
		case ComponentKind::DirectionalLight: component = node->create_component<DirectionalLightComponent>(); break;
		case ComponentKind::PointLight: component = node->create_component<PointLightComponent>(); break;
		case ComponentKind::GameCamera: component = node->create_component<GameCamera>(); break;
		case ComponentKind::LuaScript: component = node->create_component<LuaScriptComponent>(std::string{}); break;
		case ComponentKind::NavMeshVolume: component = node->create_component<NavMeshVolumeComponent>(); break;
		case ComponentKind::Collider: component = node->create_component<BoxColliderComponent>(); break;
		case ComponentKind::StaticBody: component = node->create_component<StaticRigidBodyComponent>(); break;
		case ComponentKind::DynamicBody: component = node->create_component<DynamicRigidBodyComponent>(); break;
		case ComponentKind::KinematicBody: component = node->create_component<KinematicRigidBodyComponent>(); break;
		case ComponentKind::CharacterMovement: component = node->create_component<CharacterMovementComponent>(); break;
		case ComponentKind::NavAgent: component = node->create_component<NavAgentComponent>(); break;
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

void Editor::DrawTransformTree(TransformNode& node)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	GameObject* object = node.getOwner();
	const bool isRoot = !node.get_parent();
	if (isRoot ? IsObjectSelected(object) : selectedNode == &node) flags |= ImGuiTreeNodeFlags_Selected;
	if (node.get_children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
	const bool open = ImGui::TreeNodeEx(&node, flags, isRoot ? "object %s" : "%s", node.get_name().c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		if (isRoot) SelectObject(object, settings.SelectionHeld());
		else SelectNode(&node);
	}
	if (isRoot && ImGui::BeginPopupContextItem()) {
		SelectContextObject(object);
		if (ImGui::MenuItem("Duplicate selection", settings.Label(EditorAction::Duplicate).c_str(), false, editedObject == nullptr)) requestedDuplicate = object;
		if (ImGui::MenuItem("Delete selection", settings.Label(EditorAction::Delete).c_str(), false, CanDeleteSelection())) requestedDelete = object;
		if (!CanDeleteSelection()) ImGui::TextDisabled("Selection includes the active editor camera");
		ImGui::EndPopup();
	}
	if (!isRoot && ImGui::BeginDragDropSource()) {
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
		for (TransformNode* child : node.get_children())
			if (child && !child->isPendingDestroy()) DrawTransformTree(*child);
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

void Editor::CloseTexturePreview()
{
	if (texturePreviewView) texturePreviewView->Release();
	texturePreviewView = nullptr;
	textureDescription = {}; textureFormat = DXGI_FORMAT_UNKNOWN;
	pendingTextureFile.clear(); textureFile.clear(); textureError.clear();
	showTextureTab = focusTextureTab = false;
	textureSettings = {}; textureSettingsDraft = {};
	textureSettingsReady = false;
	textureSettingsError.clear();
	textureHistory = {};
	historyTargetsTexture = textureInspectorFocused = false;
}

void Editor::OpenTexturePreview(const std::string& file)
{
	FinishTransformEdit(); FinishProperty();
	CloseTexturePreview();
	textureFile = file;
	showTextureTab = focusTextureTab = true;
	historyTargetsTexture = true;
	fitTexture = true; textureZoom = 100.0f;
	resetTextureScroll = true;
	auto* resource = game.getResourceManager().load<TextureResource>(file, game.getRenderServer().Device());
	if (!resource) { textureError = game.getResourceManager().GetLastError(); return; }
	textureSettings = textureSettingsDraft = resource->GetSettings();
	textureSettingsReady = true;
	RefreshTexturePreview(*resource);
}

void Editor::RefreshTexturePreview(TextureResource& resource)
{
	if (texturePreviewView) texturePreviewView->Release();
	texturePreviewView = nullptr; textureDescription = {}; textureError.clear();
	ID3D11ShaderResourceView* view = resource.GetTexture().SRV();
	D3D11_SHADER_RESOURCE_VIEW_DESC description{};
	view->GetDesc(&description);
	textureFormat = description.Format;
	if (description.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
		textureError = "Preview supports 2D textures. Arrays, cubemaps and volume textures are not supported yet."; return;
	}
	// GetResource и QueryInterface возвращают собственные ссылки; обе освобождаем здесь.
	ID3D11Resource* image = nullptr;
	ID3D11Texture2D* image2D = nullptr;
	view->GetResource(&image);
	const HRESULT status = image->QueryInterface(IID_PPV_ARGS(&image2D));
	image->Release();
	if (FAILED(status)) { textureError = "Cannot read texture information."; return; }
	image2D->GetDesc(&textureDescription);
	image2D->Release();
	if (!TextureFormatName(textureFormat)) {
		textureError = "This texture format is not supported by the color preview."; return;
	}
	// Кеш может исчезнуть при смене сцены; окно удерживает только одну собственную SRV-ссылку.
	view->AddRef();
	texturePreviewView = view;
}

bool Editor::TextureSettingsDirty() const
{
	return textureSettingsReady && textureSettingsDraft != textureSettings;
}

void Editor::RecordTextureSettings(const TextureSettings& before, const char* name)
{
	HistoryEntry entry;
	entry.kind = HistoryKind::TextureSettings; entry.name = name;
	entry.textureBefore = before; entry.textureAfter = textureSettingsDraft;
	PushHistory(std::move(entry), &textureHistory);
	textureSettingsError.clear();
}

void Editor::RevertTextureSettings()
{
	const auto before = textureSettingsDraft;
	textureSettingsDraft = textureSettings;
	RecordTextureSettings(before, "Revert texture settings");
}

bool Editor::ApplyTextureSettings()
{
	auto* resource = game.getResourceManager().load<TextureResource>(textureFile, game.getRenderServer().Device());
	if (!resource) { textureSettingsError = game.getResourceManager().GetLastError(); return false; }
	if (!resource->ApplySettings(textureSettingsDraft, textureSettingsError)) {
		game.getTimeManager().reset(); return false;
	}
	textureSettings = textureSettingsDraft = resource->GetSettings();
	RefreshTexturePreview(*resource);
	contentBrowser.RefreshFiles();
	game.getTimeManager().reset();
	return true;
}

void Editor::RequestTextureAction(TextureAction action, const std::string& file)
{
	// Во время подтверждения исходный запрос не заменяется другим действием.
	if (texturePromptActive) return;
	textureAction = action;
	pendingTextureFile = file;
}

void Editor::ContinueTextureAction()
{
	const auto action = textureAction;
	const auto file = std::move(pendingTextureFile);
	textureAction = TextureAction::None;
	pendingTextureFile.clear();
	if (action == TextureAction::Open) OpenTexturePreview(file);
	else if (action == TextureAction::Close) CloseTexturePreview();
	else if (action == TextureAction::HideInspector) showInspector = false;
	else if (action == TextureAction::ContinueDocument) checkPendingAction = true;
	else if (action == TextureAction::ContinueSave) requestedSave = true;
}

void Editor::ApplyTextureActions()
{
	// SRV из текущего draw list уже использован; материалы получат новую текстуру в следующем кадре.
	if (requestedTextureApply) {
		requestedTextureApply = false;
		if (!ApplyTextureSettings()) { continueAfterTextureApply = false; return; }
		if (continueAfterTextureApply) {
			continueAfterTextureApply = false;
			closeTextureDialog = true; texturePromptActive = false;
			ContinueTextureAction();
		}
	}
	if (proceedTextureAction) {
		proceedTextureAction = false; texturePromptActive = false;
		ContinueTextureAction();
	}
	if (textureAction == TextureAction::None || texturePromptActive) return;
	if (TextureSettingsDirty()) { showUnsavedTextureDialog = texturePromptActive = true; }
	else ContinueTextureAction();
}

void Editor::DrawTextureDialogs()
{
	// Закрытие Inspector через Window также проверяет черновик текстуры.
	if (!showInspector && TextureSettingsDirty()) {
		showInspector = true;
		RequestTextureAction(TextureAction::HideInspector);
	}
	if (showUnsavedTextureDialog) { showUnsavedTextureDialog = false; ImGui::OpenPopup("Unsaved texture settings"); }
	if (!ImGui::BeginPopupModal("Unsaved texture settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (closeTextureDialog) { closeTextureDialog = false; ImGui::CloseCurrentPopup(); }
	else {
		ImGui::TextUnformatted("Apply texture settings before continuing?");
		ImGui::TextWrapped("%s", textureFile.c_str());
		if (!textureSettingsError.empty()) ImGui::TextWrapped("%s", textureSettingsError.c_str());
		if (ImGui::Button("Apply and continue")) { requestedTextureApply = continueAfterTextureApply = true; }
		ImGui::SameLine();
		if (ImGui::Button("Discard")) {
			RevertTextureSettings();
			proceedTextureAction = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			if (textureAction == TextureAction::ContinueDocument) {
				pendingAction = PendingAction::None;
				checkPendingAction = resumeAfterSave = closeAfterReturn = openSceneAfterReturn = false;
			}
			textureAction = TextureAction::None; pendingTextureFile.clear();
			texturePromptActive = false;
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::EndPopup();
}

void Editor::DrawTextureInspector()
{
	ImGui::TextWrapped("%s", EditorFiles::Utf8Path(EditorFiles::DiskPath(textureFile).filename()).c_str());
	if (ImGui::CollapsingHeader("Information", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("%s", textureFile.c_str());
		if (ImGui::SmallButton("Copy path")) ImGui::SetClipboardText(textureFile.c_str());
		if (textureDescription.Width) {
			ImGui::Text("%u x %u | %u mip levels", textureDescription.Width, textureDescription.Height, textureDescription.MipLevels);
			const char* format = TextureFormatName(textureFormat);
			if (format) ImGui::Text("Format: %s", format);
			else ImGui::Text("DXGI format: %u", static_cast<unsigned>(textureFormat));
		}
	}
	if (textureSettingsReady && ImGui::CollapsingHeader(TextureSettingsDirty() ? "Loading settings *###TextureSettings" : "Loading settings###TextureSettings", ImGuiTreeNodeFlags_DefaultOpen)) {
		int color = static_cast<int>(textureSettingsDraft.colorSpace), mips = static_cast<int>(textureSettingsDraft.mipmaps);
		auto before = textureSettingsDraft;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##Color space", &color, "Color space: Auto\0Color space: Linear\0Color space: sRGB\0")) {
			textureSettingsDraft.colorSpace = static_cast<TextureSettings::ColorSpace>(color);
			RecordTextureSettings(before, "Texture color space");
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color space: Auto keeps the image's format. Linear is suitable for data maps; sRGB for color images.");
		ImGui::SetNextItemWidth(-1);
		before = textureSettingsDraft;
		if (ImGui::Combo("##Mipmaps", &mips, "Mipmaps: Auto\0Mipmaps: Disabled\0")) {
			textureSettingsDraft.mipmaps = static_cast<TextureSettings::Mipmaps>(mips);
			RecordTextureSettings(before, "Texture mipmaps");
		}
		ImGui::BeginDisabled(!TextureSettingsDirty());
		if (ImGui::Button("Apply")) requestedTextureApply = true;
		ImGui::SameLine();
		if (ImGui::Button("Revert")) RevertTextureSettings();
		ImGui::EndDisabled();
		ImGui::BeginDisabled(textureHistory.position == 0);
		if (ImGui::Button("Undo")) { historyTargetsTexture = true; Undo(); }
		ImGui::EndDisabled(); ImGui::SameLine();
		ImGui::BeginDisabled(textureHistory.position >= textureHistory.entries.size());
		if (ImGui::Button("Redo")) { historyTargetsTexture = true; Redo(); }
		ImGui::EndDisabled();
		if (!textureSettingsError.empty()) ImGui::TextWrapped("%s", textureSettingsError.c_str());
	}
	if (!textureError.empty()) { ImGui::TextWrapped("%s", textureError.c_str()); return; }
	if (!texturePreviewView) return;
	if (ImGui::Button("Fit")) { fitTexture = true; resetTextureScroll = true; }
	ImGui::SameLine();
	if (ImGui::Button("1:1")) { fitTexture = false; textureZoom = 100.0f; resetTextureScroll = true; }
	ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Zoom").x - 12));
	if (ImGui::DragFloat("Zoom", &textureZoom, 1.0f, 1.0f, 800.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) fitTexture = false;
	ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Background").x - 12));
	ImGui::Combo("Background", &textureBackground, "Checkerboard\0Dark\0Light\0");
	// Scrollbars позволяют рассматривать текстуру крупнее области, не затрагивая сцену.
	if (resetTextureScroll) { ImGui::SetNextWindowScroll(ImVec2(0, 0)); resetTextureScroll = false; }
	if (ImGui::BeginChild("Texture preview", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float width = static_cast<float>(textureDescription.Width), height = static_cast<float>(textureDescription.Height);
		if (fitTexture) {
			const float scale = (std::min)((std::max)(1.0f, available.x) / width, (std::max)(1.0f, available.y) / height);
			textureZoom = scale * 100.0f;
		}
		const ImVec2 size(width * textureZoom / 100.0f, height * textureZoom / 100.0f);
		ImVec2 position = ImGui::GetCursorScreenPos();
		position.x += (std::max)(0.0f, (available.x - size.x) * 0.5f);
		position.y += (std::max)(0.0f, (available.y - size.y) * 0.5f);
		ImGui::SetCursorScreenPos(position);
		auto* draw = ImGui::GetWindowDrawList();
		const ImVec2 end(position.x + size.x, position.y + size.y);
		draw->AddRectFilled(position, end, textureBackground == 2 ? IM_COL32(220, 220, 220, 255) : IM_COL32(45, 45, 45, 255));
		if (textureBackground == 0) {
			// Рисуем шахматку только в видимой области, независимо от масштаба исходника.
			const ImVec2 clipMin = draw->GetClipRectMin(), clipMax = draw->GetClipRectMax();
			const int firstX = static_cast<int>((std::max)(0.0f, clipMin.x - position.x) / 16.0f);
			const int firstY = static_cast<int>((std::max)(0.0f, clipMin.y - position.y) / 16.0f);
			const int lastX = static_cast<int>((std::min)(end.x, clipMax.x) - position.x) / 16;
			const int lastY = static_cast<int>((std::min)(end.y, clipMax.y) - position.y) / 16;
			for (int y = firstY; y <= lastY; ++y) for (int x = firstX; x <= lastX; ++x) {
				if ((x + y) % 2 == 0) continue;
				const ImVec2 cell(position.x + x * 16.0f, position.y + y * 16.0f);
				draw->AddRectFilled(cell, ImVec2((std::min)(cell.x + 16, end.x), (std::min)(cell.y + 16, end.y)), IM_COL32(90, 90, 90, 255));
			}
		}
		ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texturePreviewView)), size);
	}
	ImGui::EndChild();
}

void Editor::DrawInspector()
{
	if (ImGui::Begin("Inspector", &showInspector)) {
		if (ImGui::BeginTabBar("Inspector tabs")) {
			if (ImGui::BeginTabItem("Object")) {
				scenePanelFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				DrawObjectInspector();
				ImGui::EndTabItem();
			}
			bool open = showTextureTab;
			// Отмечаем черновик и сохраняем вкладку до решения в диалоге закрытия.
			const ImGuiTabItemFlags flags = (focusTextureTab ? ImGuiTabItemFlags_SetSelected : 0)
				| (TextureSettingsDirty() ? ImGuiTabItemFlags_UnsavedDocument : 0);
			if (showTextureTab && ImGui::BeginTabItem("Texture", &open, flags)) {
				focusTextureTab = false;
				textureInspectorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				if (textureInspectorFocused) historyTargetsTexture = true;
				DrawTextureInspector();
				ImGui::EndTabItem();
			}
			if (showTextureTab && !open) RequestTextureAction(TextureAction::Close);
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
	if (!showInspector && TextureSettingsDirty()) { showInspector = true; RequestTextureAction(TextureAction::HideInspector); }
}

void Editor::DrawObjectInspector()
{
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
			const char* labels[] = { "Mesh", "Directional light", "Point light", "GameCamera", "Lua Script", "Nav mesh volume", "Box collider", "Static body", "Dynamic body", "Kinematic body", "Character movement", "Nav agent" };
			for (int kind = 0; kind < IM_ARRAYSIZE(labels); ++kind)
				if (ImGui::MenuItem(labels[kind], nullptr, false, kind < static_cast<int>(ComponentKind::Collider) || !selectedNode->get_parent())) {
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
				const bool rootOnly = dynamic_cast<BoxColliderComponent*>(component) || dynamic_cast<RigidBodyComponent*>(component)
					|| dynamic_cast<CharacterMovementComponent*>(component) || dynamic_cast<NavAgentComponent*>(component);
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
			} else if (auto* collider = dynamic_cast<BoxColliderComponent*>(component)) {
				const auto before = ComponentProperties(*collider);
				DXVector3 size = collider->getColliderItem().size;
				// Полные длины сторон; весь жест редактирования образует одну команду Undo.
				bool changed = ImGui::DragFloat3("Size (X, Y, Z)", &size.x, 0.05f,
					0.001f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				if (changed) changed = collider->setSize(size);
				TrackComponentProperty(*collider, before, "Box collider size", changed);
				const auto beforeNavmesh = ComponentProperties(*collider);
				bool affectsNavmesh = collider->getColliderItem().affectsNavmesh;
				// Снятый флаг исключает из генерации коллайдер и меши этого объекта.
				const bool navmeshChanged = ImGui::Checkbox("Affects navmesh", &affectsNavmesh);
				if (navmeshChanged) collider->getColliderItem().affectsNavmesh = affectsNavmesh;
				TrackComponentProperty(*collider, beforeNavmesh, "Collider affects navmesh", navmeshChanged);
			} else if (dynamic_cast<EditorCamera*>(component)) ImGui::TextUnformatted("Editor camera");
			else if (dynamic_cast<GameCamera*>(component)) ImGui::TextUnformatted("GameCamera");
			else if (dynamic_cast<CameraMovementComponent*>(component)) ImGui::TextUnformatted("Camera navigation");
			else if (auto* script = dynamic_cast<LuaScriptComponent*>(component)) {
				const auto before = ComponentProperties(*script);
				const auto& path = script->getScriptPath();
				std::vector<char> buffer((std::max)(size_t(32768), path.size() + 1), '\0');
				std::copy(path.begin(), path.end(), buffer.begin());
				const bool changed = ImGui::InputTextWithHint("Script path", "res/Scripts/Player.lua", buffer.data(), buffer.size());
				if (changed) script->setScriptPath(buffer.data());
				// Весь ввод до выхода из поля образует одну команду Undo.
				TrackComponentProperty(*script, before, "Script path", changed);
			}
			else if (auto* mesh = dynamic_cast<MeshComponent*>(component)) {
				DrawMeshInspector(*mesh);
			}
			else if (auto* volume = dynamic_cast<NavMeshVolumeComponent*>(component)) {
				DrawNavMeshVolumeInspector(*volume);
			}
			else if (auto* character = dynamic_cast<CharacterMovementComponent*>(component)) {
				DrawCharacterMovementInspector(*character);
			}
			else if (auto* agent = dynamic_cast<NavAgentComponent*>(component)) {
				DrawNavAgentInspector(*agent);
			}
			else ImGui::TextUnformatted("Component");
			ImGui::PopID();
		}
	}
}

void Editor::DrawCharacterMovementInspector(CharacterMovementComponent& character)
{
	ImGui::TextUnformatted("Character movement (sphere)");
	// Каждое поле — отдельная команда Undo; значения ограничены теми же диапазонами, что и Validate.
	const auto floatSetting = [&](const char* label, float CharacterSettings::* field,
		float speed, float minimum, float maximum, const char* format) {
		const auto before = ComponentProperties(character);
		CharacterSettings settings = character.getSettings();
		bool changed = ImGui::DragFloat(label, &(settings.*field), speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
		if (changed) changed = character.setSettings(settings);
		TrackComponentProperty(character, before, label, changed);
	};
	const auto before = ComponentProperties(character);
	bool enabled = character.getCharacterItem().enabled;
	const bool enabledChanged = ImGui::Checkbox("Enabled", &enabled);
	if (enabledChanged) character.getCharacterItem().enabled = enabled;
	TrackComponentProperty(character, before, "Character enabled", enabledChanged);
	floatSetting("Radius", &CharacterSettings::radius, 0.01f, 0.01f, 100.0f, "%.2f m");
	floatSetting("Gravity scale", &CharacterSettings::gravityScale, 0.01f, 0.0f, 100.0f, "%.2f");
	floatSetting("Max slope", &CharacterSettings::maxSlope, 0.5f, 0.0f, 89.0f, "%.1f deg");
	floatSetting("Step height", &CharacterSettings::stepHeight, 0.01f, 0.0f, 10.0f, "%.2f m");
	floatSetting("Mass", &CharacterSettings::mass, 0.5f, 0.001f, 1000000.0f, "%.1f kg");
	floatSetting("Max strength", &CharacterSettings::maxStrength, 1.0f, 0.0f, 1000000000.0f, "%.0f N");
}

void Editor::DrawNavAgentInspector(NavAgentComponent& agent)
{
	ImGui::TextUnformatted("Nav agent");
	if (!agent.hasCharacter()) ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "Add Character movement to this object.");
	// Каждое поле — отдельная команда Undo; значения ограничены теми же диапазонами, что и Validate.
	const auto floatSetting = [&](const char* label, float NavAgentSettings::* field,
		float speed, float minimum, float maximum, const char* format) {
		const auto before = ComponentProperties(agent);
		NavAgentSettings settings = agent.getSettings();
		bool changed = ImGui::DragFloat(label, &(settings.*field), speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
		if (changed) changed = agent.setSettings(settings);
		TrackComponentProperty(agent, before, label, changed);
	};
	floatSetting("Speed", &NavAgentSettings::speed, 0.05f, 0.01f, 1000.0f, "%.2f m/s");
	floatSetting("Acceleration", &NavAgentSettings::acceleration, 0.1f, 0.01f, 10000.0f, "%.2f m/s2");
	floatSetting("Angular speed", &NavAgentSettings::angularSpeed, 1.0f, 0.0f, 100000.0f, "%.0f deg/s");
	floatSetting("Stopping distance", &NavAgentSettings::stoppingDistance, 0.01f, 0.0f, 100.0f, "%.2f m");
	const auto before = ComponentProperties(agent);
	NavAgentSettings settings = agent.getSettings();
	bool changed = ImGui::Checkbox("Update rotation", &settings.updateRotation);
	if (changed) changed = agent.setSettings(settings);
	TrackComponentProperty(agent, before, "Agent update rotation", changed);
}

void Editor::DrawNavMeshVolumeInspector(NavMeshVolumeComponent& volume)
{
	ImGui::TextUnformatted("Nav mesh volume");
	{
		const auto before = ComponentProperties(volume);
		DXVector3 size = volume.getSize();
		// Полные длины сторон в пространстве ветки; масштаб и поворот ветки применяются сверху.
		bool changed = ImGui::DragFloat3("Size (X, Y, Z)", &size.x, 0.05f,
			0.01f, 100000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (changed) changed = volume.setSize(size);
		TrackComponentProperty(volume, before, "Nav volume size", changed);
	}
	// Каждое поле — отдельная команда Undo; недопустимое сочетание параметров не применяется.
	const auto applySettings = [&](const NavBuildSettings& settings) {
		std::string error;
		const bool applied = volume.setSettings(settings, &error);
		navSettingsError = error;
		return applied;
	};
	const auto floatSetting = [&](const char* label, float NavBuildSettings::* field,
		float speed, float minimum, float maximum, const char* format) {
		const auto before = ComponentProperties(volume);
		NavBuildSettings settings = volume.getSettings();
		bool changed = ImGui::DragFloat(label, &(settings.*field), speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
		if (changed) changed = applySettings(settings);
		TrackComponentProperty(volume, before, label, changed);
	};
	ImGui::SeparatorText("Agent");
	floatSetting("Radius", &NavBuildSettings::agentRadius, 0.01f, 0.0f, 50.0f, "%.2f m");
	floatSetting("Height", &NavBuildSettings::agentHeight, 0.01f, 0.05f, 100.0f, "%.2f m");
	floatSetting("Max climb", &NavBuildSettings::agentMaxClimb, 0.01f, 0.0f, 100.0f, "%.2f m");
	floatSetting("Max slope", &NavBuildSettings::agentMaxSlope, 0.5f, 0.0f, 89.0f, "%.1f deg");
	if (ImGui::TreeNode("Advanced (Recast)")) {
		floatSetting("Cell size", &NavBuildSettings::cellSize, 0.005f, 0.01f, 10.0f, "%.3f m");
		floatSetting("Cell height", &NavBuildSettings::cellHeight, 0.005f, 0.01f, 10.0f, "%.3f m");
		{
			const auto before = ComponentProperties(volume);
			NavBuildSettings settings = volume.getSettings();
			bool changed = ImGui::DragInt("Tile size", &settings.tileSize, 1.0f, 16, 1024, "%d cells", ImGuiSliderFlags_AlwaysClamp);
			if (changed) changed = applySettings(settings);
			TrackComponentProperty(volume, before, "Tile size", changed);
		}
		floatSetting("Region min size", &NavBuildSettings::regionMinSize, 0.5f, 0.0f, 1000.0f, "%.0f cells");
		floatSetting("Region merge size", &NavBuildSettings::regionMergeSize, 0.5f, 0.0f, 1000.0f, "%.0f cells");
		floatSetting("Edge max length", &NavBuildSettings::edgeMaxLength, 0.1f, 0.0f, 1000.0f, "%.1f m");
		floatSetting("Edge max error", &NavBuildSettings::edgeMaxError, 0.05f, 0.1f, 10.0f, "%.2f");
		floatSetting("Detail sample distance", &NavBuildSettings::detailSampleDistance, 0.1f, 0.0f, 100.0f, "%.1f cells");
		floatSetting("Detail sample max error", &NavBuildSettings::detailSampleMaxError, 0.1f, 0.0f, 100.0f, "%.1f cells");
		ImGui::TreePop();
	}
	const ImVec4 warning(1.0f, 0.65f, 0.3f, 1.0f);
	if (!navSettingsError.empty()) ImGui::TextColored(warning, "%s", navSettingsError.c_str());

	ImGui::SeparatorText("Navmesh");
	NavigationServer& navigation = game.getNavigationServer();
	if (navBuilder.isRunning()) {
		// Счётчики обновляет рабочий поток; число тайлов известно после подготовки сетки.
		const int total = navBuilder.getTilesTotal(), done = navBuilder.getTilesDone();
		char overlay[64];
		if (total > 0) std::snprintf(overlay, sizeof(overlay), "%d / %d tiles", done, total);
		else std::snprintf(overlay, sizeof(overlay), "Preparing...");
		ImGui::ProgressBar(total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f, ImVec2(-1, 0), overlay);
		if (ImGui::Button("Cancel generation")) navBuilder.Cancel();
	} else {
		ImGui::BeginDisabled(editedObject != nullptr);
		if (ImGui::Button("Generate nav mesh")) requestedNavBuild = true;
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Build the navmesh of all volumes in this scene in the background");
	}
	bool showNavMesh = navigation.isDebugViewVisible();
	if (ImGui::Checkbox("Show navmesh", &showNavMesh)) navigation.SetDebugView(&game, showNavMesh);
	if (navigation.hasNavMesh()) {
		ImGui::Text("Loaded: %d tiles, %d polygons", navigation.getTileCount(), navigation.getPolyCount());
		// Проверка обходит всю сцену, поэтому выполняется не чаще раза в 30 кадров.
		if (Level* level = game.getLevel(); level && --navStaleCountdown <= 0) {
			navStaleCountdown = 30;
			navStale = NavMeshBuilder::ComputeInputHash(*level) != navigation.getInputHash();
		}
		if (navStale && !navBuilder.isRunning())
			ImGui::TextColored(warning, "The scene changed since generation. Regenerate the navmesh.");
	} else if (!navBuilder.isRunning()) {
		if (volume.getDataPath().empty()) ImGui::TextDisabled("The navmesh has not been generated yet.");
		else if (!navigation.getLastError().empty()) ImGui::TextColored(warning, "%s", navigation.getLastError().c_str());
	}
	if (!navBuildMessage.empty()) ImGui::TextWrapped("%s", navBuildMessage.c_str());
	if (!navBuilder.getLog().empty() && ImGui::TreeNode("Recast log")) {
		ImGui::TextUnformatted(navBuilder.getLog().c_str());
		ImGui::TreePop();
	}
	if (!volume.getDataPath().empty()) ImGui::TextDisabled("Data: %s", volume.getDataPath().c_str());
}

void Editor::ApplyNavigationActions()
{
	if (requestedNavBuild) {
		requestedNavBuild = false;
		Level* level = game.getLevel();
		std::string error;
		if (!level || editedObject) error = "Open a scene to generate its navmesh.";
		else if (temporaryProject || project.filePath.empty() || sceneFile.empty())
			error = "Save the scene before generating its navmesh.";
		else {
			// Навмеш хранится рядом со сценой: Scenes/Main.level.json -> Scenes/Main.navmesh.
			std::string relative = sceneFile;
			const std::string suffix = ".level.json";
			if (relative.size() > suffix.size() && relative.ends_with(suffix)) relative.resize(relative.size() - suffix.size());
			else if (const auto dot = relative.find_last_of('.'); dot != std::string::npos
				&& relative.find_first_of("/\\", dot) == std::string::npos) relative.resize(dot);
			relative += ".navmesh";
			// Тот же путь разрешит ResourceManager игры, запущенной с корнем проекта.
			const std::string resolved = game.getResourceManager().ResolvePath(relative);
			if (resolved.empty()) error = "Invalid navmesh path: " + relative;
			else if (navBuilder.Start(*level, resolved, error)) {
				navBuildPath = relative;
				navBuildMessage = navBuilder.getMessage();
			}
		}
		if (!error.empty()) navBuildMessage = error;
	}
	if (!navBuilder.Poll()) return;
	navBuildMessage = navBuilder.getMessage();
	navStaleCountdown = 0;
	if (navBuilder.getState() != NavMeshBuilder::State::Succeeded) return;
	std::string error;
	if (!navBuilder.Apply(game.getNavigationServer(), error)) { navBuildMessage += "\n" + error; return; }
	// Все объёмы уровня ссылаются на новый файл; изменённый путь попадает в сохранение сцены.
	if (Level* level = game.getLevel()) {
		for (GameObject* object : level->getObjects()) {
			if (!object || object->isPendingDestroy()) continue;
			for (Component* component : object->getComponents()) {
				auto* volume = dynamic_cast<NavMeshVolumeComponent*>(component);
				if (!volume || volume->isPendingDestroy() || volume->getDataPath() == navBuildPath) continue;
				volume->setDataPath(navBuildPath);
				checkSceneDirty = true;
			}
		}
	}
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
