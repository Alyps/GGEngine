#include "Editor.h"
#include "imgui_internal.h"
#include "RenderPresets.h"
#include "EditorFileUtils.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>
#include <unordered_set>
using EditorAction = EditorSettings::Action;
namespace {
	constexpr const char* ResourcePathPayload = "GG_RESOURCE_PATH";

	std::string_view DroppedResourcePath()
	{
		const auto* payload = ImGui::GetDragDropPayload();
		if (!payload || !payload->IsDataType(ResourcePathPayload) || payload->DataSize <= 1) return {};
		// Content Browser передаёт непустой путь с одним завершающим NUL.
		const auto* bytes = static_cast<const char*>(payload->Data);
		const std::string_view path(bytes, payload->DataSize - 1);
		if (bytes[path.size()] != '\0' || path.find('\0') != std::string_view::npos) return {};
		return path;
	}

	std::string LowerAssetName(std::string name)
	{
		for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		return name;
	}

	void UtilityInputText(const char* label, const char* hint, std::string& value, ImGuiInputTextFlags flags = 0)
	{
		std::vector<char> buffer(value.size() + 256, '\0');
		std::copy(value.begin(), value.end(), buffer.begin());
		if (ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size(), flags)) value = buffer.data();
	}

	void UtilityPathField(const char* label, const char* suffix, std::string& path, ImGuiInputTextFlags flags = 0)
	{
		UtilityInputText(label, suffix, path, flags);
		if (!ImGui::BeginDragDropTarget()) return;
		const std::string dropped(DroppedResourcePath());
		// Скрипты должны иметь точный суффикс runtime, кривые распознаются по имени в браузере.
		const bool supported = suffix == std::string_view(".action.lua") ? dropped.ends_with(suffix)
			: LowerAssetName(dropped).ends_with(suffix);
		if (supported && ImGui::AcceptDragDropPayload(ResourcePathPayload)) {
			if (ImGui::GetActiveID() == ImGui::GetItemID()) ImGui::ClearActiveID();
			path = dropped;
		}
		ImGui::EndDragDropTarget();
	}

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
			|| dynamic_cast<CharacterMovementComponent*>(component) || dynamic_cast<NavAgentComponent*>(component) || dynamic_cast<BoxTriggerComponent*>(component);
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
		case ComponentKind::Timer: component = node->getOwner()->createComponent<TimerComponent>(); break;
		case ComponentKind::Collider: component = node->create_component<BoxColliderComponent>(); break;
		case ComponentKind::BoxTrigger: component = node->create_component<BoxTriggerComponent>(); break;
		case ComponentKind::StaticBody: component = node->create_component<StaticRigidBodyComponent>(); break;
		case ComponentKind::DynamicBody: component = node->create_component<DynamicRigidBodyComponent>(); break;
		case ComponentKind::KinematicBody: component = node->create_component<KinematicRigidBodyComponent>(); break;
		case ComponentKind::CharacterMovement: component = node->create_component<CharacterMovementComponent>(); break;
		case ComponentKind::NavAgent: component = node->create_component<NavAgentComponent>(); break;
		case ComponentKind::UtilityAI: component = node->getOwner()->createComponent<UtilityAIComponent>(std::string{}); break;
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

void Editor::CloseAssetEditor()
{
	CloseTexturePreview();
	CloseMaterialEditor();
	utilityTable = {};
	utilityCurve = {};
	utilityPreviewInputs.clear();
	ClearUtilityAssetChecks();
	curvePointDrag = {};
	focusUtilityTable = false;
	utilityTableDetached = utilityCurveInTable = false;
	utilityTableDock = UtilityTableDock::None;
	utilityParameterPage = 0;
	if (focusAssetTab == AssetTab::UtilityCurve) focusAssetTab = AssetTab::None;
}

void Editor::CloseTexturePreview()
{
	if (texturePreviewView) texturePreviewView->Release();
	texturePreviewView = nullptr;
	textureDescription = {}; textureFormat = DXGI_FORMAT_UNKNOWN;
	textureFile.clear(); textureError.clear();
	showTextureTab = false;
	if (focusAssetTab == AssetTab::Texture) focusAssetTab = AssetTab::None;
	textureSettings = {}; textureSettingsDraft = {};
	textureSettingsReady = false;
	textureSettingsError.clear();
	textureHistory = {};
	if (historyAssetTab == AssetTab::Texture) historyAssetTab = AssetTab::None;
	if (focusedAssetTab == AssetTab::Texture) focusedAssetTab = AssetTab::None;
}

void Editor::OpenTexturePreview(const std::string& file)
{
	FinishTransformEdit(); FinishProperty();
	CloseTexturePreview();
	textureFile = file;
	showTextureTab = true;
	focusAssetTab = AssetTab::Texture;
	historyAssetTab = AssetTab::Texture;
	fitTexture = true; textureZoom = 100.0f;
	resetTextureScroll = true;
	auto* resource = game.getResourceManager().load<TextureResource>(file, game.getRenderServer().Device());
	if (!resource) { textureError = game.getResourceManager().GetLastError(); return; }
	textureSettings = textureSettingsDraft = resource->GetSettings();
	textureSettingsReady = true;
	RefreshTexturePreview(*resource);
}

void Editor::CloseMaterialEditor()
{
	showMaterialTab = materialReady = false;
	if (focusAssetTab == AssetTab::Material) focusAssetTab = AssetTab::None;
	materialFile.clear(); materialSettingsError.clear();
	materialSettings = {}; materialDraft = {};
	materialHistory = {};
	materialEditId = 0;
	materialEditName = nullptr;
	if (historyAssetTab == AssetTab::Material) historyAssetTab = AssetTab::None;
	if (focusedAssetTab == AssetTab::Material) focusedAssetTab = AssetTab::None;
}

void Editor::OpenMaterialEditor(const std::string& file)
{
	FinishTransformEdit(); FinishProperty();
	CloseMaterialEditor();
	materialFile = file;
	showMaterialTab = true;
	focusAssetTab = AssetTab::Material;
	historyAssetTab = AssetTab::Material;
	// Читаем описание даже при недоступной карте, чтобы её можно было заменить.
	materialReady = materialSettings.Load(file, materialSettingsError);
	materialDraft = materialSettings;
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

bool Editor::AssetSettingsDirty() const
{
	return MaterialSettingsDirty() || TextureSettingsDirty() || utilityCurve.Dirty() || utilityTable.Dirty();
}

bool Editor::InspectorAssetSettingsDirty() const
{
	return MaterialSettingsDirty() || TextureSettingsDirty() || (!utilityCurveInTable && utilityCurve.Dirty());
}

bool Editor::MaterialSettingsDirty() const
{
	return materialReady && materialDraft != materialSettings;
}

void Editor::RecordMaterialSettings(const MaterialData& before, const MaterialData& after, const char* name)
{
	if (before == after) return;
	HistoryEntry entry;
	entry.kind = HistoryKind::MaterialSettings;
	entry.name = name;
	entry.materialBefore = before;
	entry.materialAfter = after;
	PushHistory(std::move(entry), &materialHistory);
	materialSettingsError.clear();
}

void Editor::TrackMaterialEdit(const MaterialData& before, const char* name, bool changed)
{
	const auto id = ImGui::GetItemID();
	if (ImGui::IsItemActivated() || (changed && materialEditId != id)) {
		FinishMaterialEdit();
		materialEditId = id;
		materialEditName = name;
		materialEditBefore = materialEditAfter = before;
	}
	// Храним последнее состояние этого поля, даже если следующий виджет уже изменил черновик.
	if (materialEditId != id) return;
	if (changed) { materialEditAfter = materialDraft; materialSettingsError.clear(); }
	if (ImGui::IsItemDeactivated()) FinishMaterialEdit();
}

void Editor::FinishMaterialEdit()
{
	if (!materialEditId) return;
	RecordMaterialSettings(materialEditBefore, materialEditAfter, materialEditName);
	materialEditId = 0;
	materialEditName = nullptr;
}

void Editor::RevertMaterialSettings()
{
	FinishMaterialEdit();
	const auto before = materialDraft;
	materialDraft = materialSettings;
	RecordMaterialSettings(before, materialDraft, "Revert material settings");
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

bool Editor::ApplyMaterialSettings()
{
	auto& resources = game.getResourceManager();
	if (!resources.ApplyMaterial(materialFile, materialDraft, game.getRenderServer().Device())) {
		materialSettingsError = resources.GetLastError();
		game.getTimeManager().reset();
		return false;
	}
	materialSettings = materialDraft;
	materialSettingsError.clear();

	// После исправления файла повторяем привязку мешей, которые использовали запасной материал.
	if (auto* level = game.getLevel()) {
		const auto path = resources.ResolvePath(materialFile);
		for (auto* object : level->getObjects()) {
			if (object->isPendingDestroy()) continue;
			for (auto* component : object->getComponents()) {
				auto* mesh = dynamic_cast<MeshComponent*>(component);
				if (mesh && !mesh->isPendingDestroy() && !mesh->getMaterialError().empty()
					&& resources.ResolvePath(mesh->getMaterialPath()) == path)
					mesh->setMaterialPath(mesh->getMaterialPath());
			}
		}
	}
	game.getTimeManager().reset();
	return true;
}

void Editor::RequestAssetAction(AssetAction action, const std::string& file)
{
	// Во время подтверждения исходный запрос не заменяется другим действием.
	if (assetPrompt != AssetTab::None) return;
	if (action == AssetAction::OpenTexture && showTextureTab && file == textureFile) {
		focusAssetTab = AssetTab::Texture;
		return;
	}
	if (action == AssetAction::OpenMaterial && showMaterialTab && file == materialFile) {
		focusAssetTab = AssetTab::Material;
		return;
	}
	if (action == AssetAction::OpenUtilityTable && file == utilityTable.file) {
		focusUtilityTable = true;
		return;
	}
	const bool inTable = action == AssetAction::OpenUtilityCurveInTable;
	if ((inTable || action == AssetAction::OpenUtilityCurve) && file == utilityCurve.file) {
		ShowUtilityCurve(inTable);
		return;
	}
	assetAction = action;
	pendingAssetFile = file;
}

Editor::AssetTab Editor::PendingAssetConfirmation() const
{
	switch (assetAction) {
	case AssetAction::None: return AssetTab::None;
	case AssetAction::OpenTexture:
	case AssetAction::CloseTexture:
		return TextureSettingsDirty() ? AssetTab::Texture : AssetTab::None;
	case AssetAction::OpenMaterial:
	case AssetAction::CloseMaterial:
		return MaterialSettingsDirty() ? AssetTab::Material : AssetTab::None;
	case AssetAction::OpenUtilityTable:
	case AssetAction::CloseUtilityTable:
		return utilityTable.Dirty() ? AssetTab::UtilityTable : AssetTab::None;
	case AssetAction::OpenUtilityCurve:
	case AssetAction::OpenUtilityCurveInTable:
	case AssetAction::CloseUtilityCurve:
		return utilityCurve.Dirty() ? AssetTab::UtilityCurve : AssetTab::None;
	default:
		// Обрабатываем черновики по одному, включая центральную таблицу при смене документа.
		if (TextureSettingsDirty()) return AssetTab::Texture;
		if (MaterialSettingsDirty()) return AssetTab::Material;
		if (utilityCurve.Dirty() && (assetAction != AssetAction::HideInspector || !utilityCurveInTable)) return AssetTab::UtilityCurve;
		return assetAction != AssetAction::HideInspector && utilityTable.Dirty() ? AssetTab::UtilityTable : AssetTab::None;
	}
}

void Editor::ContinueAssetAction()
{
	const auto action = assetAction;
	const auto file = std::move(pendingAssetFile);
	assetAction = AssetAction::None;
	pendingAssetFile.clear();
	if (action == AssetAction::OpenTexture) OpenTexturePreview(file);
	else if (action == AssetAction::OpenMaterial) OpenMaterialEditor(file);
	else if (action == AssetAction::CloseTexture) CloseTexturePreview();
	else if (action == AssetAction::CloseMaterial) CloseMaterialEditor();
	else if (action == AssetAction::OpenUtilityTable) {
		utilityTable.Open(file);
		utilityPreviewInputs.clear();
		ClearUtilityAssetChecks();
		utilityParameterPage = 0;
		focusUtilityTable = true;
	}
	else if (action == AssetAction::OpenUtilityCurve || action == AssetAction::OpenUtilityCurveInTable) {
		utilityCurve.Open(file);
		utilityCurveXMin = 0; utilityCurveXMax = 1;
		std::string curveError;
		if (utilityCurve.ready && utilityCurve.draft.Validate(curveError)) {
			utilityCurveXMin = utilityCurve.draft.points.front().x;
			utilityCurveXMax = utilityCurve.draft.points.back().x;
		}
		ShowUtilityCurve(action == AssetAction::OpenUtilityCurveInTable);
	}
	else if (action == AssetAction::CloseUtilityTable) {
		utilityTable = {};
		utilityPreviewInputs.clear();
		ClearUtilityAssetChecks();
		focusUtilityTable = utilityTableDetached = false;
		utilityTableDock = UtilityTableDock::None;
		if (utilityCurveInTable) ShowUtilityCurve(false);
	}
	else if (action == AssetAction::CloseUtilityCurve) {
		utilityCurve = {};
		utilityCurveInTable = false;
		curvePointDrag = {};
		if (focusAssetTab == AssetTab::UtilityCurve) focusAssetTab = AssetTab::None;
	}
	else if (action == AssetAction::HideInspector) showInspector = false;
	else if (action == AssetAction::ContinueDocument) checkPendingAction = true;
	else if (action == AssetAction::ContinueSave) requestedSave = true;
}

void Editor::ApplyAssetActions()
{
	// Кадр уже отправлен на GPU. Изменения материала и текстуры видны со следующего кадра.
	if (requestedAssetApply != AssetTab::None) {
		const auto tab = requestedAssetApply;
		requestedAssetApply = AssetTab::None;
		const bool saved = tab == AssetTab::Material ? ApplyMaterialSettings()
			: tab == AssetTab::Texture ? ApplyTextureSettings() : ApplyUtilityAsset(tab);
		if (!saved) return;
	}
	if (assetAction == AssetAction::None) return;
	const auto next = PendingAssetConfirmation();
	if (next != AssetTab::None) {
		if (assetPrompt == AssetTab::None) showUnsavedAssetDialog = true;
		assetPrompt = next;
		return;
	}
	// Один диалог остаётся открыт, пока не обработаны все затронутые черновики.
	closeAssetDialog = assetPrompt != AssetTab::None;
	assetPrompt = AssetTab::None;
	ContinueAssetAction();
}

void Editor::DrawAssetDialogs()
{
	// Закрытие Inspector через Window также проверяет черновик ресурса.
	if (!showInspector && InspectorAssetSettingsDirty()) {
		showInspector = true;
		RequestAssetAction(AssetAction::HideInspector);
	}
	if (showUnsavedAssetDialog) { showUnsavedAssetDialog = false; ImGui::OpenPopup("Unsaved asset settings"); }
	const bool detachedAction = assetAction == AssetAction::CloseUtilityTable || assetAction == AssetAction::OpenUtilityCurveInTable
		|| (assetAction == AssetAction::CloseUtilityCurve && utilityCurveInTable);
	if (utilityTableDetached && detachedAction && ImGui::IsPopupOpen("Unsaved asset settings")) {
		// Подтверждение должно появиться над окном, из которого закрывают или меняют ресурс.
		const auto* tableWindow = ImGui::FindWindowByName("Utility AI");
		if (tableWindow && tableWindow->Viewport) {
			ImGui::SetNextWindowViewport(tableWindow->Viewport->ID);
			ImGui::SetNextWindowPos(tableWindow->Viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
	}
	if (!ImGui::BeginPopupModal("Unsaved asset settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (closeAssetDialog) { closeAssetDialog = false; ImGui::CloseCurrentPopup(); }
	else {
		const bool material = assetPrompt == AssetTab::Material;
		const bool table = assetPrompt == AssetTab::UtilityTable;
		const bool curve = assetPrompt == AssetTab::UtilityCurve;
		const auto& file = table ? utilityTable.file : curve ? utilityCurve.file : material ? materialFile : textureFile;
		const auto& error = table ? utilityTable.error : curve ? utilityCurve.error : material ? materialSettingsError : textureSettingsError;
		ImGui::TextUnformatted(table || curve ? "Save Utility AI changes before continuing?"
			: material ? "Apply material changes before continuing?" : "Apply texture settings before continuing?");
		ImGui::TextWrapped("%s", file.c_str());
		if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
		if (ImGui::Button("Apply and continue")) requestedAssetApply = assetPrompt;
		ImGui::SameLine();
		if (ImGui::Button("Discard")) {
			if (table || curve) RevertUtilityAsset(assetPrompt);
			else if (material) RevertMaterialSettings();
			else RevertTextureSettings();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			if (assetAction == AssetAction::ContinueDocument) {
				pendingAction = PendingAction::None;
				checkPendingAction = resumeAfterSave = closeAfterReturn = openSceneAfterReturn = false;
			}
			assetAction = AssetAction::None; pendingAssetFile.clear();
			assetPrompt = AssetTab::None;
			requestedAssetApply = AssetTab::None;
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
		if (ImGui::Button("Apply")) requestedAssetApply = AssetTab::Texture;
		ImGui::SameLine();
		if (ImGui::Button("Revert")) RevertTextureSettings();
		ImGui::EndDisabled();
		ImGui::BeginDisabled(textureHistory.position == 0);
		if (ImGui::Button("Undo")) { historyAssetTab = AssetTab::Texture; Undo(); }
		ImGui::EndDisabled(); ImGui::SameLine();
		ImGui::BeginDisabled(textureHistory.position >= textureHistory.entries.size());
		if (ImGui::Button("Redo")) { historyAssetTab = AssetTab::Texture; Redo(); }
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

void Editor::DrawMaterialInspector()
{
	ImGui::TextWrapped("%s", EditorFiles::Utf8Path(EditorFiles::DiskPath(materialFile).filename()).c_str());
	if (!materialSettingsError.empty()) ImGui::TextWrapped("%s", materialSettingsError.c_str());
	if (!materialReady) return;

	ImGui::SeparatorText("Texture maps");
	const auto& resources = game.getResourceManager();
	const auto resourceRoot = EditorFiles::DiskPath(resources.ResolvePath(resources.GetResourceDirectory()));
	const auto projectRoot = EditorFiles::DiskPath(project.Root());
	ImGui::TextDisabled("Paths are relative to %s.", resources.GetResourceDirectory().c_str());
	const auto drawMap = [&](const char* label, std::string& path) {
		ImGui::PushID(label);
		ImGui::TextUnformatted(label);
		const auto clearWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2;
		ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - clearWidth - ImGui::GetStyle().ItemSpacing.x));
		std::vector<char> buffer((std::max)(size_t(32768), path.size() + 1), '\0');
		std::copy(path.begin(), path.end(), buffer.begin());
		const auto before = materialDraft;
		const bool changed = ImGui::InputTextWithHint("##path", "Drop a texture here", buffer.data(), buffer.size());
		if (changed) {
			path = buffer.data();
			std::replace(path.begin(), path.end(), '\\', '/');
		}
		TrackMaterialEdit(before, label, changed);

		if (ImGui::BeginDragDropTarget()) {
			// Браузер передаёт путь от проекта. В файле материала он хранится от res.
			if (const auto source = DroppedResourcePath(); !source.empty()) {
				const auto relative = (projectRoot / EditorFiles::DiskPath(std::string(source)))
					.lexically_normal().lexically_relative(resourceRoot);
				auto extension = EditorFiles::Utf8Path(relative.extension());
				for (char& c : extension) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
				const bool image = extension == ".png" || extension == ".jpg" || extension == ".jpeg"
					|| extension == ".bmp" || extension == ".dds" || extension == ".tga" || extension == ".hdr";
				if (image && !relative.empty() && !relative.has_root_path() && *relative.begin() != L".."
					&& ImGui::AcceptDragDropPayload(ResourcePathPayload)) {
					if (ImGui::GetActiveID() == ImGui::GetID("##path")) ImGui::ClearActiveID();
					FinishMaterialEdit();
					const auto previous = materialDraft;
					path = EditorFiles::Utf8Path(relative);
					RecordMaterialSettings(previous, materialDraft, label);
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			FinishMaterialEdit();
			const auto previous = materialDraft;
			path.clear();
			RecordMaterialSettings(previous, materialDraft, label);
		}
		ImGui::PopID();
	};
	drawMap("Base Color", materialDraft.diffuse);
	drawMap("Normal", materialDraft.normal);
	drawMap("AO", materialDraft.ao);
	ImGui::TextDisabled("AO uses the red channel.");

	ImGui::SeparatorText("Surface");
	const char* samplers[] = { "wrap_linear", "wrap_point", "clamp_linear", "clamp_point" };
	if (ImGui::BeginCombo("Sampler", materialDraft.sampler.c_str())) {
		for (const char* sampler : samplers) {
			if (!ImGui::Selectable(sampler, materialDraft.sampler == sampler)) continue;
			FinishMaterialEdit();
			const auto before = materialDraft;
			materialDraft.sampler = sampler;
			RecordMaterialSettings(before, materialDraft, "Material sampler");
		}
		ImGui::EndCombo();
	}
	int mode = materialDraft.renderState == "wireframe" ? 1 : 0;
	if (ImGui::Combo("Render mode", &mode, "Opaque\0Wireframe\0")) {
		FinishMaterialEdit();
		const auto before = materialDraft;
		materialDraft.renderState = mode ? "wireframe" : "opaque";
		RecordMaterialSettings(before, materialDraft, "Material render mode");
	}
	// Один жест ввода или перетаскивания числа даёт одну команду Undo.
	auto before = materialDraft;
	bool changed = ImGui::DragFloat("Specular strength", &materialDraft.specularStrength, 0.01f,
		Material::MinSpecularStrength, Material::MaxSpecularStrength, nullptr, ImGuiSliderFlags_AlwaysClamp);
	TrackMaterialEdit(before, "Material specular strength", changed);
	before = materialDraft;
	changed = ImGui::DragFloat("Shininess", &materialDraft.shininess, 0.5f,
		Material::MinShininess, Material::MaxShininess, nullptr, ImGuiSliderFlags_AlwaysClamp);
	TrackMaterialEdit(before, "Material shininess", changed);

	if (materialEditId && ImGui::GetActiveID() != materialEditId) FinishMaterialEdit();
	ImGui::BeginDisabled(!MaterialSettingsDirty());
	if (ImGui::Button("Apply")) requestedAssetApply = AssetTab::Material;
	ImGui::SameLine();
	if (ImGui::Button("Revert")) RevertMaterialSettings();
	ImGui::EndDisabled();
	ImGui::BeginDisabled(materialHistory.position == 0);
	if (ImGui::Button("Undo")) { historyAssetTab = AssetTab::Material; Undo(); }
	ImGui::EndDisabled(); ImGui::SameLine();
	ImGui::BeginDisabled(materialHistory.position >= materialHistory.entries.size());
	if (ImGui::Button("Redo")) { historyAssetTab = AssetTab::Material; Redo(); }
	ImGui::EndDisabled();
}

void Editor::OpenUtilityAsset(const std::string& file)
{
	const auto name = LowerAssetName(file);
	if (name.ends_with(".utilityai.json")) RequestAssetAction(AssetAction::OpenUtilityTable, file);
	else if (name.ends_with(".curve.json")) RequestAssetAction(AssetAction::OpenUtilityCurve, file);
}

namespace {
bool ValidateUtilityReferences(const UtilityTableData& table, const ResourceManager& resources, std::string& error)
{
	std::unordered_set<std::string> checkedCurves;
	for (const auto& action : table.actions) {
		const auto script = resources.ResolvePath(action.scriptPath);
		std::error_code fileError;
		if (script.empty() || !std::filesystem::is_regular_file(EditorFiles::DiskPath(script), fileError)) {
			error = action.name + ": cannot find " + action.scriptPath;
			return false;
		}
		// Проверяем сохранённый файл, потому что Play загрузит его в новый кеш.
		for (const auto& path : action.curvePaths) {
			if (path.empty()) continue;
			const auto resolved = resources.ResolvePath(path);
			if (!checkedCurves.insert(resolved).second) continue;
			UtilityCurveData curve;
			if (!curve.Read(resolved, error) || !curve.Validate(error)) {
				error = action.name + ": " + path + ": " + error;
				return false;
			}
		}
	}
	return true;
}
} // namespace

bool Editor::ApplyUtilityAsset(AssetTab tab)
{
	if (tab == AssetTab::UtilityCurve) {
		if (!utilityCurve.Save()) return false;
		utilityCurveChecks.clear();
		return true;
	}
	if (tab != AssetTab::UtilityTable || !utilityTable.ready) return false;
	auto& asset = utilityTable;
	return asset.draft.Validate(asset.error)
		&& ValidateUtilityReferences(asset.draft, game.getResourceManager(), asset.error)
		&& asset.Save();
}

void Editor::RevertUtilityAsset(AssetTab tab)
{
	if (tab == AssetTab::UtilityTable) {
		utilityTable.Revert();
		utilityPreviewInputs.clear();
	}
	else if (tab == AssetTab::UtilityCurve) utilityCurve.Revert();
}

void Editor::DrawUtilitySaveControls(AssetTab tab, bool dirty)
{
	// В откреплённом окне у таблицы и кривой разные дочерние панели и свой фокус.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
		focusedAssetTab = historyAssetTab = tab;
		if (saveShortcut) {
			requestedSave = false;
			if (dirty) requestedAssetApply = tab;
		}
	}
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("Save")) requestedAssetApply = tab;
	ImGui::SameLine();
	if (ImGui::Button("Revert")) RevertUtilityAsset(tab);
	ImGui::EndDisabled();
	ImGui::SameLine();
	const bool table = tab == AssetTab::UtilityTable;
	ImGui::BeginDisabled(!(table ? utilityTable.CanStep(-1) : utilityCurve.CanStep(-1)));
	if (ImGui::Button("Undo")) { historyAssetTab = tab; Undo(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!(table ? utilityTable.CanStep(1) : utilityCurve.CanStep(1)));
	if (ImGui::Button("Redo")) { historyAssetTab = tab; Redo(); }
	ImGui::EndDisabled();
}

void Editor::ShowUtilityCurve(bool inTable)
{
	// Оба места показывают один черновик. Перенос не читает файл повторно.
	utilityCurveInTable = inTable && utilityTableDetached;
	curvePointDrag = {};
	if (utilityCurveInTable) {
		if (focusAssetTab == AssetTab::UtilityCurve) focusAssetTab = AssetTab::None;
		focusUtilityTable = true;
	} else {
		showInspector = true;
		focusAssetTab = AssetTab::UtilityCurve;
	}
}

void Editor::DrawUtilityTable()
{
	if (utilityTable.file.empty()) return;
	ImGuiWindowClass windowClass;
	windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
	ImGui::SetNextWindowClass(&windowClass);
	if (utilityTableDock == UtilityTableDock::Detach) {
		ImGui::SetNextWindowDockID(0);
		const auto* main = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(main->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(980, 620));
	} else if (viewportDockId) {
		ImGui::SetNextWindowDockID(viewportDockId, utilityTableDock == UtilityTableDock::Attach ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
	}
	utilityTableDock = UtilityTableDock::None;
	const bool focusWindow = focusUtilityTable;
	if (focusWindow) { ImGui::SetNextWindowFocus(); focusUtilityTable = false; }
	bool open = true;
	const ImGuiWindowFlags windowFlags = utilityTable.Dirty() ? ImGuiWindowFlags_UnsavedDocument : 0;
	const bool visible = ImGui::Begin("Utility AI", &open, windowFlags);
	utilityTableDetached = !ImGui::IsWindowDocked();
	if (!utilityTableDetached && utilityCurveInTable) ShowUtilityCurve(false);
	if (visible) {
		ImGui::BeginDisabled(utilityTableDetached && !viewportDockId);
		if (ImGui::SmallButton(utilityTableDetached ? "Dock to Viewport" : "Detach")) {
			utilityTableDock = utilityTableDetached ? UtilityTableDock::Attach : UtilityTableDock::Detach;
			focusUtilityTable = true;
		}
		ImGui::EndDisabled();
		if (utilityCurveInTable && !utilityCurve.file.empty()) {
			const float width = ImGui::GetContentRegionAvail().x;
			const float tableWidth = std::floor(width * 0.65f);
			if (ImGui::BeginChild("##detachedTable", ImVec2(tableWidth, 0))) DrawUtilityTableContents();
			ImGui::EndChild();
			ImGui::SameLine();
			if (focusWindow) ImGui::SetNextWindowFocus();
			if (ImGui::BeginChild("##detachedCurve", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
				if (ImGui::SmallButton("Close curve")) RequestAssetAction(AssetAction::CloseUtilityCurve);
				DrawUtilityCurve();
			}
			ImGui::EndChild();
		} else {
			DrawUtilityTableContents();
		}
	}
	ImGui::End();
	if (!open) RequestAssetAction(AssetAction::CloseUtilityTable);
}

void Editor::DrawUtilityTableContents()
{
	ImGui::TextWrapped("%s", EditorFiles::Utf8Path(EditorFiles::DiskPath(utilityTable.file).filename()).c_str());
	if (!utilityTable.error.empty()) ImGui::TextWrapped("%s", utilityTable.error.c_str());
	if (!utilityTable.ready) return;
	DrawUtilitySaveControls(AssetTab::UtilityTable, utilityTable.Dirty());
	auto& data = utilityTable.draft;
	const auto before = data;
	utilityPreviewInputs.resize(data.parameters.size());
	ImGui::Checkbox("Score preview", &utilityScorePreview);
	if (utilityScorePreview) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset preview values"))
			std::fill(utilityPreviewInputs.begin(), utilityPreviewInputs.end(), std::nullopt);
		ImGui::TextWrapped("Scores use saved curves. Lua availability and switching rules are checked in Play.");
	}
	if (ImGui::CollapsingHeader("Decision settings")) {
		ImGui::InputDouble("Interval (s)", &data.interval, 0.01, 0.1, "%.6g");
		ImGui::InputDouble("Switch threshold", &data.switchThreshold, 0.01, 0.1, "%.6g");
		ImGui::InputDouble("Minimum duration (s)", &data.minimumDuration, 0.01, 0.1, "%.6g");
	}
	if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) DrawUtilityParameters(data);
	if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) DrawUtilityActions(data);
	utilityTable.RecordEdit(before, ImGui::GetActiveID());
}

void Editor::DrawUtilityParameters(UtilityTableData& data)
{
	if (ImGui::Button("Add parameter")) {
		data.parameters.push_back({ "", UtilityParameterType::Float, 0 });
		utilityPreviewInputs.emplace_back();
		for (auto& action : data.actions) action.curvePaths.emplace_back();
	}
	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
	if (!ImGui::BeginTable("##ParameterFields", utilityScorePreview ? 4 : 3, flags)) return;
	ImGui::TableSetupColumn("Name");
	ImGui::TableSetupColumn("Type");
	ImGui::TableSetupColumn("Default");
	if (utilityScorePreview) ImGui::TableSetupColumn("Preview value");
	ImGui::TableHeadersRow();
	const auto& style = ImGui::GetStyle();
	const float removeWidth = ImGui::CalcTextSize("Remove").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
	size_t remove = data.parameters.size();
	for (size_t i = 0; i < data.parameters.size(); ++i) {
		auto& parameter = data.parameters[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-1);
		UtilityInputText("##name", "Parameter name", parameter.name);
		ImGui::TableNextColumn();
		int type = parameter.type == UtilityParameterType::Int ? 1 : 0;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##type", &type, "float\0int\0")) parameter.type = type ? UtilityParameterType::Int : UtilityParameterType::Float;
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - removeWidth));
		ImGui::InputDouble("##default", &parameter.initialValue, 0, 0, "%.12g");
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove")) remove = i;
		if (utilityScorePreview) {
			ImGui::TableNextColumn();
			// Без введённого значения предпросмотр следует за текущим Default.
			double probe = utilityPreviewInputs[i].value_or(parameter.initialValue);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputDouble("##probe", &probe, 0, 0, "%.12g")) utilityPreviewInputs[i] = probe;
		}
		ImGui::PopID();
	}
	ImGui::EndTable();
	if (remove == data.parameters.size()) return;
	// Индекс параметра определяет ячейку каждой строки в JSON.
	data.parameters.erase(data.parameters.begin() + remove);
	utilityPreviewInputs.erase(utilityPreviewInputs.begin() + remove);
	for (auto& action : data.actions) action.curvePaths.erase(action.curvePaths.begin() + remove);
}

void Editor::ClearUtilityAssetChecks()
{
	utilityCurveChecks.clear();
	utilityScriptChecks.clear();
}

const Editor::UtilityCurveCheck& Editor::CheckUtilityCurve(const std::string& path)
{
	// Диагностика и предпросмотр используют один результат чтения сохранённой кривой.
	auto [entry, inserted] = utilityCurveChecks.try_emplace(path);
	auto& curve = entry->second;
	if (inserted && curve.data.Read(game.getResourceManager().ResolvePath(path), curve.error)) curve.data.Validate(curve.error);
	return curve;
}

const Editor::UtilityScriptCheck& Editor::CheckUtilityScript(const std::string& path)
{
	auto [entry, inserted] = utilityScriptChecks.try_emplace(path);
	auto& script = entry->second;
	if (!inserted) return script;
	if (!path.ends_with(".action.lua")) {
		script.error = "Assign a .action.lua script to this action.";
		return script;
	}
	script.resolved = game.getResourceManager().ResolvePath(path);
	std::error_code error;
	if (script.resolved.empty() || !std::filesystem::is_regular_file(EditorFiles::DiskPath(script.resolved), error))
		script.error = "Cannot find script: " + path;
	return script;
}

std::vector<Editor::UtilityActionPreview> Editor::EvaluateUtilityPreview(const UtilityTableData& data)
{
	std::vector<UtilityActionPreview> results;
	results.reserve(data.actions.size());
	for (const auto& action : data.actions) {
		auto& result = results.emplace_back();
		result.contributions.resize(data.parameters.size());
		double sum = 0;
		size_t count = 0;
		bool complete = true;
		for (size_t i = 0; i < action.curvePaths.size(); ++i) {
			const auto& path = action.curvePaths[i];
			if (path.empty()) continue;
			const auto& curve = CheckUtilityCurve(path);
			const auto& parameter = data.parameters[i];
			const double value = utilityPreviewInputs[i].value_or(parameter.initialValue);
			const bool invalidInt = parameter.type == UtilityParameterType::Int
				&& (std::trunc(value) != value || value < INT32_MIN || value > INT32_MAX);
			if (!curve.error.empty() || !std::isfinite(value) || invalidInt) { complete = false; continue; }
			const double contribution = curve.data.Evaluate(value);
			result.contributions[i] = contribution;
			sum += contribution;
			++count;
		}
		// Движок усредняет оценки только по заполненным ячейкам строки.
		if (complete && count) result.score = sum / static_cast<double>(count);
	}
	return results;
}

void Editor::DrawUtilityActions(UtilityTableData& data)
{
	if (ImGui::Button("Add action")) data.actions.push_back({ "", "", std::vector<std::string>(data.parameters.size()) });
	ImGui::SameLine();
	if (ImGui::SmallButton("Reload assets")) ClearUtilityAssetChecks();
	ImGui::TextDisabled("Drop a script or curve from Content Browser. Paths are relative to the project.");
	std::unordered_map<std::string, size_t> scriptUses;
	for (const auto& action : data.actions) {
		const auto& script = CheckUtilityScript(action.scriptPath);
		if (script.error.empty()) ++scriptUses[script.resolved];
	}
	const auto preview = utilityScorePreview ? EvaluateUtilityPreview(data) : std::vector<UtilityActionPreview>{};
	size_t leader = preview.size();
	for (size_t i = 0; i < preview.size(); ++i) {
		if (preview[i].score && (leader == preview.size() || *preview[i].score > *preview[leader].score)) leader = i;
	}
	if (utilityScorePreview) {
		if (leader < preview.size()) {
			const auto& name = data.actions[leader].name;
			ImGui::TextWrapped("Highest score: %s (%.3f)", name.empty() ? "Unnamed action" : name.c_str(), *preview[leader].score);
		} else ImGui::TextDisabled("No scores available");
	}
	// Страницы позволяют открыть таблицу с любым количеством параметров в пределах ImGui.
	constexpr size_t pageSize = 8;
	const size_t lastPage = data.parameters.empty() ? 0 : (data.parameters.size() - 1) / pageSize;
	utilityParameterPage = (std::min)(utilityParameterPage, lastPage);
	if (lastPage) {
		ImGui::BeginDisabled(utilityParameterPage == 0);
		if (ImGui::SmallButton("<")) --utilityParameterPage;
		ImGui::EndDisabled(); ImGui::SameLine();
		ImGui::Text("Parameters %zu / %zu", utilityParameterPage + 1, lastPage + 1);
		ImGui::SameLine(); ImGui::BeginDisabled(utilityParameterPage == lastPage);
		if (ImGui::SmallButton(">")) ++utilityParameterPage;
		ImGui::EndDisabled();
	}
	const size_t first = utilityParameterPage * pageSize;
	const size_t count = (std::min)(pageSize, data.parameters.size() - first);
	constexpr float actionWidth = 125.0f, scriptWidth = 175.0f, curveWidth = 155.0f;
	const auto& style = ImGui::GetStyle();
	const float minimumWidth = actionWidth + scriptWidth + curveWidth * count
		+ style.CellPadding.x * 2 * (count + 2) + style.ScrollbarSize;
	// В широкой панели столбцы заполняют таблицу, в узкой сохраняют ширину полей.
	const bool scrollX = minimumWidth > ImGui::GetContentRegionAvail().x;
	const auto columnWidth = scrollX ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;
	const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
		| ImGuiTableFlags_Resizable | (scrollX ? ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit
			: ImGuiTableFlags_SizingStretchProp);
	const float height = (std::max)(160.0f, ImGui::GetContentRegionAvail().y);
	if (!ImGui::BeginTable("##ActionMatrix", static_cast<int>(count + 2), flags, ImVec2(0, height))) return;
	ImGui::TableSetupScrollFreeze(1, 1);
	ImGui::TableSetupColumn("Action", columnWidth, actionWidth);
	ImGui::TableSetupColumn("Script", columnWidth, scriptWidth);
	for (size_t i = 0; i < count; ++i) {
		const auto& name = data.parameters[first + i].name;
		const std::string label = name.empty() ? "Parameter " + std::to_string(first + i + 1) : name;
		ImGui::TableSetupColumn(label.c_str(), columnWidth, curveWidth);
	}
	ImGui::TableHeadersRow();
	const ImVec4 errorColor(1.0f, 0.47f, 0.42f, 1.0f);
	const ImVec4 warningColor(1.0f, 0.75f, 0.36f, 1.0f);
	size_t remove = data.actions.size(), move = data.actions.size();
	for (size_t row = 0; row < data.actions.size(); ++row) {
		auto& action = data.actions[row];
		ImGui::PushID(static_cast<int>(row));
		ImGui::TableNextRow();
		if (utilityScorePreview && row == leader)
			ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header, 0.6f));
		if (ImGui::TableNextColumn()) {
			ImGui::SetNextItemWidth(-1);
			UtilityInputText("##action", "Action name", action.name);
			if (std::none_of(action.curvePaths.begin(), action.curvePaths.end(), [](const auto& path) { return !path.empty(); })) {
				ImGui::TextColored(errorColor, "No curves");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Assign at least one curve to this action.");
			}
			if (utilityScorePreview) {
				if (const auto score = preview[row].score) ImGui::Text("Score %.3f", *score);
				else ImGui::TextDisabled("Score unavailable");
			}
			// Кнопки остаются видимыми при горизонтальной прокрутке матрицы.
			ImGui::BeginDisabled(row == 0);
			if (ImGui::SmallButton("Up")) move = row;
			ImGui::EndDisabled(); ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) remove = row;
		}
		if (ImGui::TableNextColumn()) {
			ImGui::SetNextItemWidth(-1);
			UtilityPathField("##script", ".action.lua", action.scriptPath);
			const auto& script = CheckUtilityScript(action.scriptPath);
			const auto uses = scriptUses.find(script.resolved);
			if (!script.error.empty()) {
				ImGui::TextColored(errorColor, "Script error");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", script.error.c_str());
			} else if (uses != scriptUses.end() && uses->second > 1) {
				ImGui::TextColored(warningColor, "Shared script");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%zu actions use this script.", uses->second);
			}
		}
		for (size_t i = first; i < first + count; ++i) {
			if (!ImGui::TableNextColumn()) continue;
			auto& path = action.curvePaths[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.5f));
			UtilityPathField("##curve", ".curve.json", path);
			ImGui::SameLine(); ImGui::BeginDisabled(path.empty());
			if (ImGui::SmallButton("Edit")) {
				const auto resolved = game.getResourceManager().ResolvePath(path);
				if (!resolved.empty()) RequestAssetAction(utilityTableDetached ? AssetAction::OpenUtilityCurveInTable : AssetAction::OpenUtilityCurve, resolved);
			}
			ImGui::EndDisabled();
			if (!path.empty()) {
				const auto& curve = CheckUtilityCurve(path);
				if (!curve.error.empty()) {
					ImGui::TextColored(errorColor, "Curve error");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", curve.error.c_str());
				} else if (utilityScorePreview) {
					if (const auto value = preview[row].contributions[i]) ImGui::TextDisabled("Value %.3f", *value);
					else ImGui::TextDisabled("Unavailable");
				}
			}
			ImGui::PopID();
		}
		ImGui::PopID();
	}
	ImGui::EndTable();
	if (remove < data.actions.size()) data.actions.erase(data.actions.begin() + remove);
	else if (move > 0 && move < data.actions.size()) std::swap(data.actions[move], data.actions[move - 1]);
}

void Editor::DrawUtilityCurveGraph(UtilityCurveData& data)
{
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size((std::max)(16.0f, ImGui::GetContentRegionAvail().x), 120.0f);
	ImGui::InvisibleButton("##curveGraph", size);
	if (!ImGui::IsItemActive() || ImGui::IsItemActivated()) curvePointDrag.active = false;

	// Ручной диапазон остаётся неизменным при редактировании точек.
	constexpr double limit = (std::numeric_limits<double>::max)();
	const double left = utilityCurveXMin, right = utilityCurveXMax;
	const double scale = std::isfinite(right - left) ? 1.0 : 0.5;
	const float width = size.x - 8, height = size.y - 8;
	const auto normalizedX = [&](double x) { return (x * scale - left * scale) / (right * scale - left * scale); };
	const auto screen = [&](const UtilityCurveData::Point& point) {
		return ImVec2(origin.x + 4 + static_cast<float>(normalizedX(point.x)) * width,
			origin.y + 4 + static_cast<float>(1 - point.y) * height);
	};

	size_t hovered = data.points.size();
	if (ImGui::IsItemHovered()) {
		const ImVec2 mouse = ImGui::GetMousePos();
		float closest = 8.0f * 8.0f;
		for (size_t i = 0; i < data.points.size(); ++i) {
			if (data.points[i].x < left || data.points[i].x > right) continue;
			const ImVec2 point = screen(data.points[i]);
			const float dx = mouse.x - point.x, dy = mouse.y - point.y;
			const float distance = dx * dx + dy * dy;
			if (distance < closest) { hovered = i; closest = distance; }
		}
	}
	if (hovered < data.points.size() && ImGui::IsItemActivated())
		curvePointDrag = { hovered, data.points[hovered], left, right, true };

	if (curvePointDrag.active && curvePointDrag.index < data.points.size()) {
		const size_t i = curvePointDrag.index;
		const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
		const auto& start = curvePointDrag.start;
		// Берём ближайшие представимые X внутри соседей, чтобы кривая оставалась валидной.
		const double lower = i ? (std::max)(left, std::nextafter(data.points[i - 1].x, limit)) : left;
		const double upper = i + 1 < data.points.size() ? (std::min)(right, std::nextafter(data.points[i + 1].x, -limit)) : right;
		// Ограничиваем движение и границами оси, и соседними точками.
		const double u = std::clamp(normalizedX(start.x) + delta.x / width, 0.0, 1.0);
		const double x = delta.x == 0 ? start.x : std::clamp(std::lerp(left, right, u), lower, upper);
		data.points[i] = { x, std::clamp(start.y - delta.y / height, 0.0, 1.0) };
		hovered = i;
	}

	auto* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
	// График не перекрывает остальные элементы UI.
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	const ImU32 line = ImGui::GetColorU32(ImGuiCol_PlotLines);
	// Обрезаем в координатах кривой до преобразования в float экранных координат.
	ImVec2 previous = screen({ left, data.Evaluate(left) });
	for (const auto& point : data.points) {
		if (point.x <= left || point.x >= right) continue;
		const ImVec2 next = screen(point);
		draw->AddLine(previous, next, line, 2.0f);
		previous = next;
	}
	draw->AddLine(previous, screen({ right, data.Evaluate(right) }), line, 2.0f);
	for (size_t i = 0; i < data.points.size(); ++i) {
		if (data.points[i].x < left || data.points[i].x > right) continue;
		const bool highlight = i == hovered;
		draw->AddCircleFilled(screen(data.points[i]), highlight ? 5.0f : 3.0f,
			highlight ? ImGui::GetColorU32(ImGuiCol_PlotLinesHovered) : line);
	}
	draw->PopClipRect();
	if (hovered < data.points.size()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		const auto& point = data.points[hovered];
		ImGui::SetTooltip("X (input) %.6g | Y (output) %.6g", point.x, point.y);
	}
	ImGui::TextDisabled("X (input) %.6g to %.6g | Y (output) 0 to 1", left, right);
}

void Editor::DrawUtilityCurve()
{
	ImGui::TextWrapped("%s", EditorFiles::Utf8Path(EditorFiles::DiskPath(utilityCurve.file).filename()).c_str());
	if (!utilityCurve.error.empty()) ImGui::TextWrapped("%s", utilityCurve.error.c_str());
	if (!utilityCurve.ready) return;
	DrawUtilitySaveControls(AssetTab::UtilityCurve, utilityCurve.Dirty());
	ImGui::TextWrapped("X: input parameter value (any finite number). Y: output utility (0 to 1).");
	ImGui::TextWrapped("X limits control the graph and dragging. Points outside the view are preserved and can be edited below.");
	auto& data = utilityCurve.draft;
	const auto before = data;
	std::string error;
	const bool validCurve = data.Validate(error);
	bool rangeChanged = ImGui::InputDouble("X min", &utilityCurveXMin, 0, 0, "%.12g");
	rangeChanged |= ImGui::InputDouble("X max", &utilityCurveXMax, 0, 0, "%.12g");
	ImGui::BeginDisabled(!validCurve);
	if (ImGui::Button("Fit X to points")) {
		utilityCurveXMin = data.points.front().x;
		utilityCurveXMax = data.points.back().x;
		rangeChanged = true;
	}
	ImGui::EndDisabled();
	if (rangeChanged) curvePointDrag = {};
	// Ошибочный диапазон не допускается до деления на его ширину.
	const bool validRange = std::isfinite(utilityCurveXMin) && std::isfinite(utilityCurveXMax)
		&& utilityCurveXMin < utilityCurveXMax;
	if (!validRange) ImGui::TextWrapped("X limits must be finite, with min less than max.");
	if (!validCurve) ImGui::TextWrapped("%s", error.c_str());
	if (validCurve && validRange) DrawUtilityCurveGraph(data);
	// Ищем самый широкий видимый промежуток с представимым числом внутри.
	std::optional<double> newPointX;
	double widest = -1;
	if (validRange && (validCurve || data.points.size() < 2)) {
		const double scale = std::isfinite(utilityCurveXMax - utilityCurveXMin) ? 1.0 : 0.5;
		const auto considerGap = [&](double left, double right) {
			const double middle = std::lerp(left, right, 0.5);
			const double width = right * scale - left * scale;
			if (middle > left && middle < right && width > widest) {
				newPointX = middle;
				widest = width;
			}
		};
		double left = utilityCurveXMin;
		for (const auto& point : data.points) {
			if (point.x <= left) continue;
			if (point.x >= utilityCurveXMax) break;
			considerGap(left, point.x);
			left = point.x;
		}
		considerGap(left, utilityCurveXMax);
	}
	ImGui::BeginDisabled(!newPointX.has_value());
	if (ImGui::Button("Add point")) {
		const double x = *newPointX;
		const double y = validCurve ? data.Evaluate(x) : 0;
		const auto position = std::lower_bound(data.points.begin(), data.points.end(), x,
			[](const auto& point, double value) { return point.x < value; });
		// Вставка на существующую кривую сохраняет её форму и порядок точек.
		data.points.insert(position, { x, y });
		curvePointDrag = {};
	}
	ImGui::EndDisabled();
	if (!newPointX && validRange && validCurve) ImGui::TextDisabled("No room for another point within X limits.");
	bool reorderPoints = false;
	if (ImGui::BeginTable("Points", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("X (input)");
		ImGui::TableSetupColumn("Y (output)");
		ImGui::TableSetupColumn("##controls", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();
		size_t remove = data.points.size();
		for (size_t i = 0; i < data.points.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
			// Фиксируем X по Enter, чтобы промежуточный ввод не ломал кривую.
			char xText[64]{};
			std::to_chars(xText, xText + sizeof(xText) - 1, data.points[i].x,
				std::chars_format::general, std::numeric_limits<double>::max_digits10);
			// EnterReturnsTrue поддерживается InputText, но запрещён для InputDouble.
			if (ImGui::InputText("##X", xText, sizeof(xText),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific)) {
				const std::string_view text(xText);
				const char* first = xText + (text.starts_with('+') ? 1 : 0);
				double x = 0;
				const auto parsed = std::from_chars(first, xText + text.size(), x, std::chars_format::general);
				const bool validNumber = parsed.ec == std::errc{} && parsed.ptr == xText + text.size();
				bool duplicate = false;
				for (size_t j = 0; j < data.points.size(); ++j)
					if (j != i && data.points[j].x == x) duplicate = true;
				if (!validNumber || !std::isfinite(x) || duplicate) utilityCurve.error = "X must be finite and different from other points. Value unchanged.";
				else {
					data.points[i].x = x;
					reorderPoints = true;
					utilityCurve.error.clear();
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press Enter to apply X. Points are sorted automatically.");
			ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
			ImGui::InputDouble("##Y", &data.points[i].y, 0, 0, "%.12g");
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Remove")) remove = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (remove < data.points.size()) data.points.erase(data.points.begin() + remove);
	}
	if (reorderPoints) {
		std::sort(data.points.begin(), data.points.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
		curvePointDrag = {};
		ImGui::ClearActiveID();
	}
	utilityCurve.RecordEdit(before, ImGui::GetActiveID());
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
			const auto drawAssetTab = [&](const char* label, AssetTab tab, bool visible, bool dirty,
				AssetAction close, void (Editor::*draw)()) {
				if (!visible) return;
				// Открытая вкладка остаётся видимой до решения в диалоге закрытия.
				bool open = true;
				const ImGuiTabItemFlags flags = (focusAssetTab == tab ? ImGuiTabItemFlags_SetSelected : 0)
					| (dirty ? ImGuiTabItemFlags_UnsavedDocument : 0);
				if (ImGui::BeginTabItem(label, &open, flags)) {
					if (focusAssetTab == tab) {
						focusAssetTab = AssetTab::None;
						historyAssetTab = tab;
					}
					if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
						focusedAssetTab = historyAssetTab = tab;
					(this->*draw)();
					ImGui::EndTabItem();
				}
				if (!open) RequestAssetAction(close);
			};
			drawAssetTab("Texture", AssetTab::Texture, showTextureTab, TextureSettingsDirty(),
				AssetAction::CloseTexture, &Editor::DrawTextureInspector);
			drawAssetTab("Material", AssetTab::Material, showMaterialTab, MaterialSettingsDirty(),
				AssetAction::CloseMaterial, &Editor::DrawMaterialInspector);
			drawAssetTab("Curve", AssetTab::UtilityCurve, !utilityCurveInTable && !utilityCurve.file.empty(), utilityCurve.Dirty(),
				AssetAction::CloseUtilityCurve, &Editor::DrawUtilityCurve);
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
	if (!showInspector && InspectorAssetSettingsDirty()) { showInspector = true; RequestAssetAction(AssetAction::HideInspector); }
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
			const char* labels[] = { "Mesh", "Directional light", "Point light", "GameCamera", "Lua Script", "Nav mesh volume", "Timer", "Box collider", "Static body", "Dynamic body", "Kinematic body", "Character movement", "Nav agent", "Box trigger", "Utility AI" };
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
					|| dynamic_cast<CharacterMovementComponent*>(component) || dynamic_cast<NavAgentComponent*>(component) || dynamic_cast<BoxTriggerComponent*>(component);
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
			} else if (auto* timer = dynamic_cast<TimerComponent*>(component)) {
				auto before = ComponentProperties(*timer);
				float seconds = timer->getWaitTime();
				bool changed = ImGui::DragFloat("Wait time (seconds)", &seconds, 0.05f,
					0.001f, (std::numeric_limits<float>::max)(), "%.3f", ImGuiSliderFlags_AlwaysClamp);
				if (changed) changed = timer->setWaitTime(seconds);
				TrackComponentProperty(*timer, before, "Timer wait time", changed);
				// Отдельный снимок каждого поля сохраняет привычные шаги Undo/Redo.
				before = ComponentProperties(*timer);
				bool repeat = timer->isRepeating();
				changed = ImGui::Checkbox("Repeat", &repeat);
				if (changed) timer->setRepeating(repeat);
				TrackComponentProperty(*timer, before, "Timer repeat", changed);
				before = ComponentProperties(*timer);
				bool automatic = timer->isAutostart();
				changed = ImGui::Checkbox("Autostart", &automatic);
				if (changed) timer->setAutostart(automatic);
				TrackComponentProperty(*timer, before, "Timer autostart", changed);
			} else if (auto* trigger = dynamic_cast<BoxTriggerComponent*>(component)) {
				auto before = ComponentProperties(*trigger);
				DXVector3 size = trigger->getTriggerItem().size;
				bool changed = ImGui::DragFloat3("Size (X, Y, Z)", &size.x, 0.05f,
					0.001f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				if (changed) changed = trigger->setSize(size);
				TrackComponentProperty(*trigger, before, "Box trigger size", changed);
				before = ComponentProperties(*trigger);
				changed = ImGui::Checkbox("Enabled", &trigger->getTriggerItem().enabled);
				TrackComponentProperty(*trigger, before, "Box trigger enabled", changed);
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
			else if (auto* ai = dynamic_cast<UtilityAIComponent*>(component)) DrawUtilityComponentInspector(*ai);
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

void Editor::DrawUtilityComponentInspector(UtilityAIComponent& component)
{
	bool enabled = component.IsEnabled();
	if (ImGui::Checkbox("Enabled", &enabled)) {
		BeginProperty(HistoryKind::Component, Id(&component), ComponentProperties(component), "Utility AI enabled");
		component.SetEnabled(enabled);
		FinishProperty();
		checkSceneDirty = true;
	}

	ImGui::TextUnformatted("Utility AI table");
	ImGui::SetNextItemWidth(-1);
	std::string path = component.GetTablePath();
	// Ввод фиксируется по Enter. Перетаскивание сразу даёт готовый путь от проекта.
	UtilityPathField("##table", ".utilityai.json", path, ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drop a table or enter a project-relative path and press Enter.");
	ImGui::BeginDisabled(path.empty());
	if (ImGui::Button("Edit table")) {
		const auto resolved = game.getResourceManager().ResolvePath(path);
		if (!resolved.empty()) OpenUtilityAsset(resolved);
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear")) path.clear();
	ImGui::EndDisabled();

	if (path != component.GetTablePath()) {
		if (!path.empty() && !LowerAssetName(path).ends_with(".utilityai.json")) {
			sceneActionError = "Expected a .utilityai.json file.";
		} else {
			sceneActionError.clear();
			BeginProperty(HistoryKind::Component, Id(&component), ComponentProperties(component), "Utility AI table");
			std::replace(path.begin(), path.end(), '\\', '/');
			component.SetTablePath(game, std::move(path));
			FinishProperty();
			checkSceneDirty = true;
		}
	}
	if (!component.GetLastError().empty()) ImGui::TextWrapped("%s", component.GetLastError().c_str());
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
			const std::string value(DroppedResourcePath());
			if (!value.empty() && supported(value) && ImGui::AcceptDragDropPayload(ResourcePathPayload)) apply(value);
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
