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
	enum class PendingAction { None, Close, OpenProject, NewProject, OpenObject, ReturnScene };
	PendingAction pendingAction = PendingAction::None;
	bool checkPendingAction = false, proceedPendingAction = false;
	bool showUnsavedDialog = false, closeUnsavedDialog = false;
	bool resumeAfterSave = false, requestedSave = false, saveShortcut = false;
	bool scenePanelFocused = false, checkSceneDirty = false, sceneDirty = true;
	std::string savedScene, documentError;
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
	void ClearTransformHistory();
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
	void PickObject(const ImVec2& origin, const ImVec2& size);
	void DrawSelectionBounds(const ImVec2& origin, const ImVec2& size);
	void DrawGizmo(const ImVec2& origin, const ImVec2& size);
	bool ResizeViewport(UINT width, UINT height);
	void ReleaseViewport();
	const char* ObjectLabel(GameObject& object) const;
	CameraMovementComponent* GetCameraMovement() const;

	// История заимствует адреса и сбрасывается перед удалением объектов.
	struct TransformState {
		GameObject* object = nullptr;
		TransformNode* node = nullptr;
		DXVector3 position, scale;
		DXQuaternion rotation;
	};
	struct TransformEdit { TransformState before, after; };
	TransformState CaptureTransform(GameObject* object, TransformNode* node) const;
	void TrackTransformEdit(const TransformState& before);
	void FinishTransformEdit();
	void ApplyTransform(const TransformState& state);
	void Undo();
	void Redo();
	std::vector<TransformEdit> history;
	size_t historyPosition = 0;
	TransformState pendingEdit;
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
