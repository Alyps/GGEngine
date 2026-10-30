#pragma once
#include <core.h>
#include "imgui.h"
#include "ImGuizmo.h"
#include "ContentBrowser.h"
#include "Project.h"
#include <unordered_map>

// Game и сцена живут дольше редактора; редактор владеет только UI и viewport.
class Editor {
public:
	explicit Editor(Game& game);
	~Editor();
	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;
	bool Init();
	int Run();
	// Пути UTF-8; сцена относительно проекта, exe и проект относительно cwd редактора.
	// true означает создание процесса, а не успешную загрузку его сцены.
	bool LaunchGame(const std::string& executablePath, const std::string& projectDirectory,
		const std::string& startLevelPath, std::string& error);

private:
	static LRESULT CALLBACK HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
	void DrawDockspace();
	void DrawProjectDialogs();
	void QueueProjectOpen(const std::string& file);
	void ApplyProjectAction();
	void RequestClose();
	void HandleShortcuts();
	void ApplyDocumentActions();
	void ContinuePendingAction();
	bool SaveScene(Project& destination, std::string& error);
	bool CaptureScene(std::string& snapshot, std::string& error) const;
	void RefreshSceneDirty();
	enum class PendingAction { None, Close, OpenProject, NewProject, OpenObject, ReturnScene, OpenScene };
	PendingAction pendingAction = PendingAction::None;
	bool checkPendingAction = false, proceedPendingAction = false;
	bool showUnsavedDialog = false, closeUnsavedDialog = false;
	bool resumeAfterSave = false, requestedSave = false, saveShortcut = false;
	bool scenePanelFocused = false, checkSceneDirty = false, sceneDirty = true;
	std::string savedScene, documentError;
	std::string sceneFile, pendingSceneFile;
	bool requestedSceneOpen = false, openSceneAfterReturn = false;
	void QueueSceneOpen(const std::string& file);
	Project projectCreationTarget;
	bool creationSavesCurrentScene = false;
	// Во время редактирования ассета исходная сцена хранится как описание, без живых указателей.
	std::string parkedScene, parkedBaseline, objectFile, pendingObjectFile;
	GameObject* editedObject = nullptr;
	bool requestedObjectOpen = false, requestedSceneReturn = false, closeAfterReturn = false;
	GameObject* requestedObjectExport = nullptr;
	std::string exportObjectPath;
	void QueueObjectOpen(const std::string& file);
	void ApplyObjectActions();
	bool ReplaceEditorLevel(Level* level, EditorCamera* camera, std::string& error);
	void ResetSceneReferences();
	bool SaveObject(std::string& error);
	void DrawTreeInspector();
	void ApplyTreeAction();
	void ClearDocumentHistory();
	enum class TreeOperation { None, Add, Rename, Remove, Reparent, MoveComponent, AddComponent, RemoveComponent, Inheritance };
	enum class ComponentKind { Mesh, DirectionalLight, PointLight, Camera, Navigation, Collider, StaticBody, DynamicBody };
	struct TreeAction {
		TreeOperation operation = TreeOperation::None;
		TransformNode* node = nullptr;
		TransformNode* target = nullptr;
		Component* component = nullptr;
		ComponentKind kind = ComponentKind::Mesh;
		std::string name;
		bool inheritPosition = true, inheritRotation = true, inheritScale = true;
	} treeAction;
	TransformNode* nameDraftNode = nullptr;
	std::string nodeNameDraft;
	Project project, pendingProject;
	ProjectHistory recentProjects;
	std::string projectError;
	std::string projectCreationError;
	bool projectCreationCompleted = false;
	bool initializeDemo = false;
	bool temporaryProject = false;
	char newProjectName[128] = "NewProject";
	char newProjectParent[2048]{};
	bool showNewProject = false;
	bool requestedProjectSwitch = false;
	bool requestedProjectCreation = false;
	void DrawHierarchy();
	enum class ObjectType { None, Empty, Cube, Sphere, Plane, DirectionalLight, PointLight, Mesh };
	void DrawCreateMenu();
	void ApplySceneActions();
	bool CanDeleteObject(GameObject* object) const;
	ObjectType requestedCreate = ObjectType::None;
	GameObject* requestedDelete = nullptr;
	GameObject* requestedDuplicate = nullptr;
	std::string sceneActionError;
	void DrawTransformTree(TransformNode& node, int index);
	void DrawInspector();
	void DrawMeshInspector(MeshComponent& mesh);
	// Черновики ввода принадлежат UI; компонент получает только подтверждённые пути.
    struct MeshPathDraft {
        std::string mesh, material;
        std::string appliedMesh, appliedMaterial;
        std::string meshError, materialError;
    };
    std::unordered_map<MeshComponent*, MeshPathDraft> meshPathDrafts;
	void DrawViewport();
	void DrawViewportToolbar();
	void PickObject(const ImVec2& origin, const ImVec2& size, bool toggle = false, bool context = false);
	void SelectObject(GameObject* object, bool toggle = false);
	void SelectContextObject(GameObject* object);
	std::vector<GameObject*> SelectedObjects() const;
	bool CanDeleteSelection() const;
	void SelectNode(TransformNode* node);
	bool IsObjectSelected(GameObject* object) const;
	void DrawSelectionBounds(const ImVec2& origin, const ImVec2& size);
	void DrawGizmo(const ImVec2& origin, const ImVec2& size);
	bool ResizeViewport(UINT width, UINT height);
	void ReleaseViewport();
	const char* ObjectLabel(GameObject& object) const;
	CameraMovementComponent* GetCameraMovement() const;

	// Сырые адреса допустимы только пока идёт текущий жест; записи истории используют ID.
	struct TransformState {
		GameObject* object = nullptr;
		TransformNode* node = nullptr;
		DXVector3 position, scale;
		DXQuaternion rotation;
	};
	using HistoryId = uint64_t;
	// Выделение заимствует объекты через ID; порядок определяет последний активный объект.
	std::vector<HistoryId> selectedObjects;
	enum class TargetKind { Object, Node, Component };
	enum class HistoryKind { Node, Component, Structure, Group };
	struct TargetIdentity {
		TargetKind kind = TargetKind::Node;
		HistoryId id = 0;
		std::vector<size_t> branch;
		size_t component = 0;
		bool ordinary = false;
	};
	struct ObjectState {
		HistoryId id = 0;
		size_t position = 0;
		std::string json;
		std::vector<TargetIdentity> identities;
	};
	struct HistoryEntry {
		HistoryKind kind = HistoryKind::Node;
		HistoryId target = 0;
		std::string name, before, after;
		ObjectState objectBefore, objectAfter;
		HistoryId selectionBefore = 0, selectionAfter = 0, cameraBefore = 0, cameraAfter = 0;
		std::vector<HistoryId> selectedBefore, selectedAfter;
		std::vector<HistoryEntry> children;
	};
	struct DocumentHistory {
		std::vector<HistoryEntry> entries;
		size_t position = 0;
		std::vector<ObjectState> identities;
	} documentHistory, parkedHistory;
	std::unordered_map<GameObject*, HistoryId> objectIds;
	std::unordered_map<TransformNode*, HistoryId> nodeIds;
	std::unordered_map<Component*, HistoryId> componentIds;
	struct HistoryTarget { TargetKind kind; void* pointer; };
	std::unordered_map<HistoryId, HistoryTarget> historyTargets;
	HistoryId nextHistoryId = 1;
	HistoryId Id(GameObject* object);
	HistoryId Id(TransformNode* node);
	HistoryId Id(Component* component);
	void ForgetDeadHistoryTargets();
	void ForgetObjectHistoryTargets(GameObject* object);
	ObjectState CaptureObjectState(GameObject* object);
	void RegisterObjectState(GameObject* object, const ObjectState& state);
	void RegisterNodeState(TransformNode& node, const std::vector<size_t>& path, ObjectState& state);
	void RestoreDocumentIdentities(const DocumentHistory& history);
	void CaptureDocumentIdentities(DocumentHistory& history);
	std::string NodeProperties(TransformNode& node, const TransformState* pose = nullptr);
	std::string ComponentProperties(Component& component);
	bool ApplyProperties(const HistoryEntry& entry, const std::string& json);
	void PushHistory(HistoryEntry entry);
	void BeginProperty(HistoryKind kind, HistoryId target, const std::string& before, const char* name);
	void FinishProperty(const std::string* after = nullptr);
	void TrackComponentProperty(Component& component, const std::string& before, const char* name, bool changed);
	HistoryEntry pendingProperty;
	bool editingProperty = false;
	bool BeginStructure(GameObject* object, const char* name, bool creating = false);
	void FinishStructures();
	std::vector<HistoryEntry> pendingStructures;
	int requestedHistoryStep = 0;
	void ApplyHistoryStep();
	bool RestoreObjectState(const ObjectState& state, HistoryId target);
	bool RestoreObjectStates(std::vector<ObjectState> states);
	bool ApplyHistoryEntry(const HistoryEntry& entry, bool undo);
	void RestoreSelectedObjects(const std::vector<HistoryId>& objects, HistoryId active);
	void RestoreHistorySelection(HistoryId selection, HistoryId camera);
	TransformState CaptureTransform(GameObject* object, TransformNode* node) const;
	void TrackTransformEdit(const TransformState& before);
	void FinishTransformEdit();
	void Undo();
	void Redo();
	TransformState pendingEdit;
	std::vector<TransformState> pendingGroup;
	void CaptureTransformGroup(const TransformState& before);
	bool ApplyTransformGroup(const DXMatrix& delta);
	DXMatrix TransformMatrix(const TransformState& state) const;
	DXMatrix groupGizmoStart, groupGizmoCurrent;
	bool editingTransform = false;
	bool editingGizmo = false;

	Game& game;
	ContentBrowser contentBrowser;
	bool showContentBrowser = true;
	ImGuiContext* ui = nullptr;
	WNDPROC previousProcedure = nullptr;
	bool platformReady = false;
	bool rendererReady = false;
	bool navigating = false;
	bool showHierarchy = true;
	bool showInspector = true;
	bool resetLayout = false;
	bool layoutInitialized = false;

	// Выбор не владеет объектом или узлом и очищается перед их удалением.
	GameObject* selectedObject = nullptr;
	TransformNode* selectedNode = nullptr;
	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	ImGuizmo::MODE mode = ImGuizmo::WORLD;
	bool useSnap = false;
	float snap = 1.0f;

	// COM-ссылки освобождаются явно, в том числе при частичной инициализации.
	ID3D11Texture2D* colorTexture = nullptr;
	ID3D11RenderTargetView* colorTarget = nullptr;
	ID3D11ShaderResourceView* colorView = nullptr;
	ID3D11Texture2D* depthTexture = nullptr;
	ID3D11DepthStencilView* depthTarget = nullptr;
	D3D11_VIEWPORT viewport{};
};
