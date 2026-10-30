#include "Editor.h"
#include "imgui_internal.h"
#include <cmath>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
namespace {
	std::string CompactJson(const rapidjson::Value& value)
	{
		if (value.IsNull()) return {};
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		if (!value.Accept(writer)) return {};
		return std::string(buffer.GetString(), buffer.GetSize());
	}
}

void Editor::ClearDocumentHistory()
{
	FinishTransformEdit(); FinishProperty();
	documentHistory = {};
	objectIds.clear(); nodeIds.clear(); componentIds.clear(); historyTargets.clear();
	pendingStructures.clear(); requestedHistoryStep = 0;
	pendingEdit = {}; editingTransform = editingGizmo = false;
	pendingGroup.clear();
}

Editor::HistoryId Editor::Id(GameObject* object)
{
	if (!object) return 0;
	auto [entry, inserted] = objectIds.try_emplace(object, nextHistoryId);
	if (inserted) { ++nextHistoryId; historyTargets.emplace(entry->second, HistoryTarget{ TargetKind::Object, object }); }
	return entry->second;
}
Editor::HistoryId Editor::Id(TransformNode* node)
{
	if (!node) return 0;
	auto [entry, inserted] = nodeIds.try_emplace(node, nextHistoryId);
	if (inserted) { ++nextHistoryId; historyTargets.emplace(entry->second, HistoryTarget{ TargetKind::Node, node }); }
	return entry->second;
}
Editor::HistoryId Editor::Id(Component* component)
{
	if (!component) return 0;
	auto [entry, inserted] = componentIds.try_emplace(component, nextHistoryId);
	if (inserted) { ++nextHistoryId; historyTargets.emplace(entry->second, HistoryTarget{ TargetKind::Component, component }); }
	return entry->second;
}

void Editor::ForgetObjectHistoryTargets(GameObject* object)
{
	// Удаляем только заимствованные адреса, пока сам объект и его дерево ещё живы.
	for (auto it = componentIds.begin(); it != componentIds.end();) {
		if (it->first->getOwner() == object) { historyTargets.erase(it->second); it = componentIds.erase(it); } else ++it;
	}
	for (auto it = nodeIds.begin(); it != nodeIds.end();) {
		if (it->first->getOwner() == object) { historyTargets.erase(it->second); it = nodeIds.erase(it); } else ++it;
	}
	if (const auto it = objectIds.find(object); it != objectIds.end()) { historyTargets.erase(it->second); objectIds.erase(it); }
}

void Editor::ForgetDeadHistoryTargets()
{
	for (auto it = componentIds.begin(); it != componentIds.end();) {
		if (it->first->isPendingDestroy() || it->first->getOwner()->isPendingDestroy()) {
			historyTargets.erase(it->second); it = componentIds.erase(it);
		} else ++it;
	}
	for (auto it = nodeIds.begin(); it != nodeIds.end();) {
		if (it->first->isPendingDestroy() || it->first->getOwner()->isPendingDestroy()) {
			historyTargets.erase(it->second); it = nodeIds.erase(it);
		} else ++it;
	}
	for (auto it = objectIds.begin(); it != objectIds.end();) {
		if (it->first->isPendingDestroy()) { historyTargets.erase(it->second); it = objectIds.erase(it); } else ++it;
	}
}

void Editor::RegisterNodeState(TransformNode& node, const std::vector<size_t>& path, ObjectState& state)
{
	if (node.isPendingDestroy()) return;
	state.identities.push_back({ TargetKind::Node, Id(&node), path });
	size_t index = 0;
	for (auto* component : node.get_components()) if (!component->isPendingDestroy())
		state.identities.push_back({ TargetKind::Component, Id(component), path, index++ });
	index = 0;
	for (auto* child : node.get_children()) if (!child->isPendingDestroy()) {
		auto childPath = path; childPath.push_back(index++);
		RegisterNodeState(*child, childPath, state);
	}
}

Editor::ObjectState Editor::CaptureObjectState(GameObject* object)
{
	ObjectState state;
	state.id = Id(object);
	const auto& objects = game.getLevel()->getObjects();
	state.position = static_cast<size_t>(std::find(objects.begin(), objects.end(), object) - objects.begin());
	rapidjson::Document document;
	state.json = CompactJson(object->Serialize(document.GetAllocator(), documentError));
	RegisterNodeState(object->getTransform(), {}, state);
	size_t index = 0;
	for (auto* component : object->getComponents())
		if (!component->isPendingDestroy() && !dynamic_cast<TransformableComponent*>(component))
			state.identities.push_back({ TargetKind::Component, Id(component), {}, index++, true });
	return state;
}

void Editor::RegisterObjectState(GameObject* object, const ObjectState& state)
{
	ForgetObjectHistoryTargets(object);
	objectIds[object] = state.id;
	historyTargets[state.id] = { TargetKind::Object, object };
	std::vector<Component*> ordinary;
	for (auto* component : object->getComponents())
	{
		if (!dynamic_cast<TransformableComponent*>(component)) ordinary.push_back(component);
		if (auto* movement = dynamic_cast<CameraMovementComponent*>(component)) {
			ConfigureCameraNavigation(*movement);
		}
	}
	for (const auto& identity : state.identities) {
		auto* node = &object->getTransform();
		for (size_t index : identity.branch) {
			if (!node || index >= node->get_children().size()) { node = nullptr; break; }
			node = node->get_children()[index];
		}
		if (!node) continue;
		if (identity.kind == TargetKind::Node) {
			nodeIds[node] = identity.id;
			historyTargets[identity.id] = { TargetKind::Node, node };
		} else {
			Component* component = identity.ordinary
				? (identity.component < ordinary.size() ? ordinary[identity.component] : nullptr)
				: (identity.component < node->get_components().size() ? node->get_components()[identity.component] : nullptr);
			if (component) { componentIds[component] = identity.id; historyTargets[identity.id] = { TargetKind::Component, component }; }
		}
	}
}

void Editor::CaptureDocumentIdentities(DocumentHistory& context)
{
	context.identities.clear();
	if (!game.getLevel()) return;
	for (auto* object : game.getLevel()->getObjects()) {
		ObjectState state = CaptureObjectState(object);
		state.json.clear(); // Для парковки документа достаточно ID и расположения узлов.
		context.identities.push_back(std::move(state));
	}
}
void Editor::RestoreDocumentIdentities(const DocumentHistory& context)
{
	objectIds.clear(); nodeIds.clear(); componentIds.clear(); historyTargets.clear();
	const auto& objects = game.getLevel()->getObjects();
	for (const auto& state : context.identities)
		if (state.position < objects.size()) RegisterObjectState(objects[state.position], state);
}

std::string Editor::NodeProperties(TransformNode& node, const TransformState* pose)
{
	rapidjson::Document document;
	document.SetObject(); auto& allocator = document.GetAllocator();
	const auto position = pose ? pose->position : node.get_position();
	const auto rotation = pose ? pose->rotation : node.get_rotation();
	const auto scale = pose ? pose->scale : node.get_scale();
	document.AddMember("name", Serialization::String(node.get_name(), allocator), allocator);
	document.AddMember("parent", Id(node.get_parent()), allocator);
	document.AddMember("position", Serialization::Array({ position.x, position.y, position.z }, allocator), allocator);
	document.AddMember("rotation", Serialization::Array({ rotation.x, rotation.y, rotation.z, rotation.w }, allocator), allocator);
	document.AddMember("scale", Serialization::Array({ scale.x, scale.y, scale.z }, allocator), allocator);
	document.AddMember("inherit_position", node.is_inherit_position(), allocator);
	document.AddMember("inherit_rotation", node.is_inherit_rotation(), allocator);
	document.AddMember("inherit_scale", node.is_inherit_scale(), allocator);
	return CompactJson(document);
}
std::string Editor::ComponentProperties(Component& component)
{
	rapidjson::Document document;
	return CompactJson(component.Serialize(document.GetAllocator(), documentError));
}

bool Editor::ApplyProperties(const HistoryEntry& entry, const std::string& text)
{
	const auto found = historyTargets.find(entry.target);
	if (found == historyTargets.end()) { documentError = "Undo target is no longer available."; return false; }
	rapidjson::Document json;
	json.Parse<rapidjson::kParseValidateEncodingFlag>(text.data(), text.size());
	if (json.HasParseError()) { documentError = "Invalid history data."; return false; }
	if (!json.IsObject()) return false;
	if (entry.kind == HistoryKind::Node && found->second.kind == TargetKind::Node) {
		auto* node = static_cast<TransformNode*>(found->second.pointer);
		std::string name; float p[3], r[4], s[3]; bool ip, ir, is;
		using namespace Deserialization;
		if (!String(json, "name", name, documentError) || !Floats(json, "position", p, 3, documentError)
			|| !Floats(json, "rotation", r, 4, documentError) || !Floats(json, "scale", s, 3, documentError)
			|| !Bool(json, "inherit_position", ip, documentError) || !Bool(json, "inherit_rotation", ir, documentError)
			|| !Bool(json, "inherit_scale", is, documentError) || !json.HasMember("parent") || !json["parent"].IsUint64()) return false;
		TransformNode* parent = nullptr;
		if (const HistoryId id = json["parent"].GetUint64(); id != 0) {
			const auto target = historyTargets.find(id);
			if (target == historyTargets.end() || target->second.kind != TargetKind::Node) return false;
			parent = static_cast<TransformNode*>(target->second.pointer);
		}
		if (name.empty() || (parent && parent->find_child(name) && parent->find_child(name) != node)) return false;
		if (node->get_parent() != parent && (!parent || !node->reparent(*parent))) return false;
		if (!node->set_name(name)) return false;
		node->set_position(DXVector3(p)); node->set_rotation(DXQuaternion(r[0], r[1], r[2], r[3])); node->set_scale(DXVector3(s));
		node->set_inherit_position(ip); node->set_inherit_rotation(ir); node->set_inherit_scale(is);
		if (!IsObjectSelected(node->getOwner())) SelectNode(node);
		else { selectedObject = node->getOwner(); selectedNode = node; }
		return true;
	}
	if (entry.kind == HistoryKind::Component && found->second.kind == TargetKind::Component) {
		auto* component = static_cast<Component*>(found->second.pointer);
		if (auto* light = dynamic_cast<LightComponent*>(component)) {
			float color[3], intensity, range = 0; bool enabled;
			if (!Deserialization::Floats(json, "color", color, 3, documentError)
				|| !Deserialization::Float(json, "intensity", intensity, documentError)
				|| !Deserialization::Bool(json, "enabled", enabled, documentError)) return false;
			auto* point = dynamic_cast<PointLightComponent*>(light);
			if (point && !Deserialization::Float(json, "range", range, documentError)) return false;
			light->color = { color[0], color[1], color[2] }; light->intensity = intensity; light->enabled = enabled;
			if (point) point->range = range;
			if (game.isEditorMode()) light->Update(game, 0);
			return true;
		}
		if (auto* mesh = dynamic_cast<MeshComponent*>(component)) return mesh->ApplyDescription(json, documentError);
	}
	documentError = "Unsupported history operation.";
	return false;
}

void Editor::PushHistory(HistoryEntry entry)
{
	if (entry.kind == HistoryKind::Group ? entry.children.empty()
		: entry.kind == HistoryKind::Structure ? entry.objectBefore.json == entry.objectAfter.json : entry.before == entry.after) return;
	auto& entries = documentHistory.entries;
	entries.erase(entries.begin() + documentHistory.position, entries.end());
	entries.push_back(std::move(entry));
	// Ограничиваем количество и память; одна большая операция остаётся отменяемой.
	const auto objectBytes = [](const ObjectState& state) {
		size_t bytes = state.json.size() + state.identities.size() * sizeof(TargetIdentity);
		for (const auto& identity : state.identities) bytes += identity.branch.size() * sizeof(size_t);
		return bytes;
	};
	const auto entryBytes = [&](const auto& self, const HistoryEntry& action) -> size_t {
		size_t bytes = sizeof(HistoryEntry) + action.name.size() + action.before.size() + action.after.size()
			+ (action.selectedBefore.size() + action.selectedAfter.size()) * sizeof(HistoryId)
			+ objectBytes(action.objectBefore) + objectBytes(action.objectAfter);
		for (const auto& child : action.children) bytes += self(self, child);
		return bytes;
	};
	size_t bytes = 0;
	for (const auto& action : entries) bytes += entryBytes(entryBytes, action);
	while (entries.size() > 1 && (entries.size() > 128 || bytes > 64ull * 1024 * 1024)) {
		bytes -= entryBytes(entryBytes, entries.front());
		entries.erase(entries.begin());
	}
	documentHistory.position = entries.size();
	checkSceneDirty = true;
}

void Editor::BeginProperty(HistoryKind kind, HistoryId target, const std::string& before, const char* name)
{
	FinishTransformEdit(); FinishProperty();
	pendingProperty = {}; pendingProperty.kind = kind; pendingProperty.target = target;
	pendingProperty.before = before; pendingProperty.name = name;
	editingProperty = !before.empty();
}
void Editor::FinishProperty(const std::string* overrideAfter)
{
	if (!editingProperty) return;
	editingProperty = false;
	auto entry = std::move(pendingProperty); pendingProperty = {};
	const auto found = historyTargets.find(entry.target);
	if (found == historyTargets.end()) return;
	entry.after = overrideAfter ? *overrideAfter
		: entry.kind == HistoryKind::Node ? NodeProperties(*static_cast<TransformNode*>(found->second.pointer))
		: ComponentProperties(*static_cast<Component*>(found->second.pointer));
	if (entry.after.empty()) { ApplyProperties(entry, entry.before); return; }
	PushHistory(std::move(entry));
}
void Editor::TrackComponentProperty(Component& component, const std::string& before, const char* name, bool changed)
{
	if (changed) checkSceneDirty = true;
	const HistoryId target = Id(&component);
	if (ImGui::IsItemActivated() || changed) {
		if (!editingProperty || pendingProperty.target != target || pendingProperty.name != name) {
			FinishTransformEdit();
			if (editingProperty && pendingProperty.target == target) FinishProperty(&before); else FinishProperty();
			BeginProperty(HistoryKind::Component, target, before, name);
		}
	}
	if (editingProperty && pendingProperty.target == target && pendingProperty.name == name && ImGui::IsItemDeactivated()) FinishProperty();
}

bool Editor::BeginStructure(GameObject* object, const char* name, bool creating)
{
	FinishTransformEdit(); FinishProperty();
	HistoryEntry entry; entry.kind = HistoryKind::Structure; entry.name = name; entry.target = Id(object);
	entry.objectBefore.id = entry.target;
	if (!creating) {
		entry.objectBefore = CaptureObjectState(object);
		if (entry.objectBefore.json.empty()) return false;
	}
	entry.selectionBefore = Id(selectedNode);
	entry.selectedBefore = selectedObjects;
	entry.cameraBefore = Id(static_cast<Component*>(game.getRenderServer().getCamera()));
	pendingStructures.push_back(std::move(entry));
	return true;
}
void Editor::FinishStructures()
{
	auto records = std::move(pendingStructures); pendingStructures.clear();
	HistoryEntry group; group.kind = HistoryKind::Group;
	if (!records.empty()) {
		group.name = records.front().name;
		group.selectedBefore = records.front().selectedBefore;
		group.selectionBefore = records.front().selectionBefore;
		group.cameraBefore = records.front().cameraBefore;
	}
	for (auto& entry : records) {
		entry.objectAfter.id = entry.target;
		const auto found = historyTargets.find(entry.target);
		if (found != historyTargets.end()) {
			auto* object = static_cast<GameObject*>(found->second.pointer);
			for (auto* component : object->getComponents()) if (auto* movement = dynamic_cast<CameraMovementComponent*>(component)) {
				ConfigureCameraNavigation(*movement);
			}
			entry.objectAfter = CaptureObjectState(object);
			if (entry.objectAfter.json.empty()) { documentError = "Cannot record the changed object."; continue; }
		}
		entry.selectionAfter = Id(selectedNode);
		entry.selectedAfter = selectedObjects;
		entry.cameraAfter = Id(static_cast<Component*>(game.getRenderServer().getCamera()));
		if (records.size() == 1) PushHistory(std::move(entry));
		else if (entry.objectBefore.json != entry.objectAfter.json) group.children.push_back(std::move(entry));
	}
	if (records.size() > 1) {
		group.selectedAfter = selectedObjects; group.selectionAfter = Id(selectedNode);
		group.cameraAfter = Id(static_cast<Component*>(game.getRenderServer().getCamera()));
		PushHistory(std::move(group));
	}
}

bool Editor::RestoreObjectState(const ObjectState& state, HistoryId target)
{
	ObjectState description = state; description.id = target;
	return RestoreObjectStates({ std::move(description) });
}

bool Editor::RestoreObjectStates(std::vector<ObjectState> states)
{
	auto* level = game.getLevel();
	// Сначала готовим весь пакет: ошибка десериализации не должна менять ни один объект.
	std::stable_sort(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
	std::vector<GameObject*> restored(states.size(), nullptr);
	for (size_t i = 0; i < states.size(); ++i) {
		if (states[i].json.empty()) continue;
		rapidjson::Document json; json.Parse(states[i].json.data(), states[i].json.size());
		if (!json.HasParseError()) restored[i] = GameObject::Deserialize(json, game.getComponentFactoryMap(), documentError);
		if (!restored[i]) {
			for (auto* object : restored) delete object;
			return false;
		}
	}
	for (size_t i = 0; i < states.size(); ++i) {
		if (!restored[i] || level->addObjectAt(restored[i], states[i].position)) continue;
		// Уровень уже владеет предыдущими элементами очереди; удаляем их через его API.
		for (size_t j = 0; j < i; ++j) if (restored[j]) level->destroyObject(restored[j]);
		for (size_t j = i; j < restored.size(); ++j) delete restored[j];
		game.EndFrame(); documentError = "Cannot restore object ownership."; return false;
	}
	selectedObject = nullptr; selectedNode = nameDraftNode = nullptr; selectedObjects.clear();
	requestedObjectExport = nullptr;
	meshPathDrafts.clear(); treeAction = {}; ImGui::ClearActiveID(); ImGui::ClearDragDrop();
	for (size_t i = 0; i < states.size(); ++i) {
		const auto found = historyTargets.find(states[i].id);
		if (found == historyTargets.end() || found->second.kind != TargetKind::Object) continue;
		auto* previous = static_cast<GameObject*>(found->second.pointer);
		if (editedObject == previous) editedObject = restored[i];
		ForgetObjectHistoryTargets(previous); level->destroyObject(previous);
	}
	// Весь пакет удаляется и возвращается за один EndFrame с сохранением авторского порядка.
	game.EndFrame();
	for (size_t i = 0; i < states.size(); ++i) if (restored[i]) RegisterObjectState(restored[i], states[i]);
	return true;
}

void Editor::RestoreSelectedObjects(const std::vector<HistoryId>& objects, HistoryId active)
{
	selectedObjects.clear(); selectedObject = nullptr; selectedNode = nameDraftNode = nullptr;
	for (HistoryId id : objects) {
		const auto found = historyTargets.find(id);
		if (found == historyTargets.end() || found->second.kind != TargetKind::Object) continue;
		auto* object = static_cast<GameObject*>(found->second.pointer);
		if (!object->isPendingDestroy()) { selectedObjects.push_back(id); selectedObject = object; selectedNode = &object->getTransform(); }
	}
	const auto found = historyTargets.find(active);
	if (found != historyTargets.end() && found->second.kind == TargetKind::Node) {
		auto* node = static_cast<TransformNode*>(found->second.pointer);
		if (IsObjectSelected(node->getOwner())) { selectedNode = node; selectedObject = node->getOwner(); }
	}
}

bool Editor::ApplyHistoryEntry(const HistoryEntry& entry, bool undo)
{
	bool applied = false;
	if (entry.kind == HistoryKind::Group) {
		if (entry.children.empty()) return false;
		if (entry.children.front().kind == HistoryKind::Structure) {
			std::vector<ObjectState> states;
			for (const auto& child : entry.children) states.push_back(undo ? child.objectBefore : child.objectAfter);
			applied = RestoreObjectStates(std::move(states));
		} else {
			// Проверяем все адресаты до изменения первой позы; откат готовится из живых значений.
			std::vector<std::string> rollback;
			for (const auto& child : entry.children) {
				const auto found = historyTargets.find(child.target);
				if (found == historyTargets.end() || found->second.kind != TargetKind::Node) return false;
				rollback.push_back(NodeProperties(*static_cast<TransformNode*>(found->second.pointer)));
			}
			size_t changed = 0;
			for (; changed < entry.children.size(); ++changed)
				if (!ApplyProperties(entry.children[changed], undo ? entry.children[changed].before : entry.children[changed].after)) break;
			applied = changed == entry.children.size();
			if (!applied) for (size_t i = 0; i < changed; ++i) ApplyProperties(entry.children[i], rollback[i]);
		}
	} else if (entry.kind == HistoryKind::Structure) {
		applied = RestoreObjectState(undo ? entry.objectBefore : entry.objectAfter, entry.target);
	} else return ApplyProperties(entry, undo ? entry.before : entry.after);
	if (applied) {
		RestoreHistorySelection(undo ? entry.selectionBefore : entry.selectionAfter, undo ? entry.cameraBefore : entry.cameraAfter);
		RestoreSelectedObjects(undo ? entry.selectedBefore : entry.selectedAfter, undo ? entry.selectionBefore : entry.selectionAfter);
	}
	return applied;
}
void Editor::RestoreHistorySelection(HistoryId selection, HistoryId camera)
{
	if (const auto found = historyTargets.find(selection); found != historyTargets.end() && found->second.kind == TargetKind::Node) {
		SelectNode(static_cast<TransformNode*>(found->second.pointer));
	}
	const auto found = historyTargets.find(camera);
	if (found != historyTargets.end() && found->second.kind == TargetKind::Component)
		game.getRenderServer().setCamera(dynamic_cast<EditorCamera*>(static_cast<Component*>(found->second.pointer)));
	if (auto* movement = GetCameraMovement()) { ConfigureCameraNavigation(*movement); }
}

void Editor::ApplyHistoryStep()
{
	const int direction = requestedHistoryStep; requestedHistoryStep = 0;
	auto& context = documentHistory;
	if (!direction || (direction < 0 ? context.position == 0 : context.position >= context.entries.size())) return;
	const size_t index = direction < 0 ? context.position - 1 : context.position;
	const auto& entry = context.entries[index];
	const bool applied = ApplyHistoryEntry(entry, direction < 0);
	if (!applied) { if (documentError.empty()) documentError = "Cannot apply history operation."; return; }
	context.position = direction < 0 ? index : index + 1;
	meshPathDrafts.clear(); nameDraftNode = nullptr; checkSceneDirty = true; game.getTimeManager().reset();
}

Editor::TransformState Editor::CaptureTransform(GameObject* object, TransformNode* node) const
{
	return { object, node, node->get_position(), node->get_scale(), node->get_rotation() };
}
DXMatrix Editor::TransformMatrix(const TransformState& state) const
{
	return DXMatrix::CreateScale(state.scale) * DXMatrix::CreateFromQuaternion(state.rotation) * DXMatrix::CreateTranslation(state.position);
}

DXVector3 Editor::SelectionCenter(const TransformState* before) const
{
	const auto objects = SelectedObjects();
	DXVector3 center = DXVector3::Zero;
	for (auto* object : objects) center += before && before->object == object ? before->position : object->getTransform().get_position();
	return objects.empty() ? center : center / static_cast<float>(objects.size());
}

bool Editor::IsValidEditorScale(const DXVector3& scale)
{
	return std::isfinite(scale.x) && std::isfinite(scale.y) && std::isfinite(scale.z)
		&& scale.x >= MinimumScale && scale.y >= MinimumScale && scale.z >= MinimumScale;
}

bool Editor::ApplyTransformGroupStep(const TransformState& before, bool fromGroupCenter)
{
	const auto after = CaptureTransform(before.object, before.node);
	DXMatrix original = TransformMatrix(before), changed = TransformMatrix(after);
	original.Translation(DXVector3::Zero); changed.Translation(DXVector3::Zero);
	const float determinant = original.Determinant();
	if (!std::isfinite(determinant) || determinant == 0.0f) return false;
	const DXVector3 center = fromGroupCenter ? SelectionCenter(&before) : before.position;
	// Pivot выбирается вызывающей панелью; сдвиг остаётся в мировых координатах.
	const DXMatrix step = DXMatrix::CreateTranslation(-center) * original.Invert() * changed
		* DXMatrix::CreateTranslation(center + after.position - before.position);
	const DXMatrix delta = groupEditDelta * step;
	if (!ApplyTransformGroup(delta)) return false;
	groupEditDelta = delta;
	return true;
}

void Editor::CaptureTransformGroup(const TransformState& before)
{
	pendingGroup.clear();
	groupEditDelta = DXMatrix::Identity;
	if (selectedObjects.size() < 2 || before.node->get_parent()) return;
	for (auto* object : SelectedObjects())
		pendingGroup.push_back(object == before.object ? before : CaptureTransform(object, &object->getTransform()));
}

bool Editor::ApplyTransformGroup(const DXMatrix& delta)
{
	std::vector<TransformState> poses;
	for (const auto& before : pendingGroup) {
		TransformState pose = before;
		DXMatrix matrix = TransformMatrix(before) * delta;
		if (!matrix.Decompose(pose.scale, pose.rotation, pose.position)) return false;
		if (!IsValidEditorScale(pose.scale)) {
			sceneActionError = "Scale must be positive and at least 0.001 on every axis.";
			return false;
		}
		pose.rotation.Normalize();
		const DXMatrix rebuilt = TransformMatrix(pose);
		// Не применяем приближённую S/R/T, если общий масштаб создаёт сдвиг осей.
		for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
			if (!std::isfinite(matrix.m[row][column]) || std::abs(matrix.m[row][column] - rebuilt.m[row][column])
				> 0.0001f * (1.0f + std::abs(matrix.m[row][column]))) {
				sceneActionError = "Group transform would introduce shear. Use uniform scale for rotated objects.";
				return false;
			}
		poses.push_back(pose);
	}
	// Сначала проверены все позы; только теперь меняем исходные объекты.
	for (const auto& pose : poses) {
		pose.node->set_position(pose.position); pose.node->set_rotation(pose.rotation); pose.node->set_scale(pose.scale);
	}
	sceneActionError.clear(); checkSceneDirty = true;
	return true;
}

void Editor::TrackTransformEdit(const TransformState& before, bool fromGroupCenter)
{
	if (ImGui::IsItemActivated() || (ImGui::IsItemActive() && !editingTransform)) {
		FinishTransformEdit(); FinishProperty(); pendingEdit = before;
		CaptureTransformGroup(before); editingTransform = true;
	}
	const bool changed = before.node && (before.position != before.node->get_position()
		|| before.rotation != before.node->get_rotation() || before.scale != before.node->get_scale());
	if (changed) {
		checkSceneDirty = true;
		if (!pendingGroup.empty() && !ApplyTransformGroupStep(before, fromGroupCenter)) {
			before.node->set_position(before.position); before.node->set_rotation(before.rotation); before.node->set_scale(before.scale);
		}
	}
	if (ImGui::IsItemDeactivated()) FinishTransformEdit();
}
void Editor::FinishTransformEdit()
{
	if (!editingTransform) return;
	editingTransform = editingGizmo = false;
	if (!pendingEdit.object || pendingEdit.object->isPendingDestroy() || pendingEdit.node->isPendingDestroy()) {
		pendingEdit = {}; pendingGroup.clear(); return;
	}
	const auto record = [&](const TransformState& before) {
		HistoryEntry entry; entry.kind = HistoryKind::Node; entry.target = Id(before.node); entry.name = "Transform";
		entry.before = NodeProperties(*before.node, &before); entry.after = NodeProperties(*before.node);
		return entry;
	};
	if (pendingGroup.empty()) PushHistory(record(pendingEdit));
	else {
		HistoryEntry group; group.kind = HistoryKind::Group; group.name = "Transform selection";
		group.selectedBefore = group.selectedAfter = selectedObjects;
		group.selectionBefore = group.selectionAfter = Id(pendingEdit.node);
		for (const auto& before : pendingGroup) {
			auto entry = record(before);
			if (entry.before != entry.after) group.children.push_back(std::move(entry));
		}
		PushHistory(std::move(group));
	}
	pendingEdit = {}; pendingGroup.clear();
}
void Editor::Undo()
{
	if (!editingTransform && !editingProperty && documentHistory.position > 0) requestedHistoryStep = -1;
}
void Editor::Redo()
{
	if (!editingTransform && !editingProperty && documentHistory.position < documentHistory.entries.size()) requestedHistoryStep = 1;
}
