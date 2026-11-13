#pragma once
#include <core.h>
#include "imgui.h"
#include "ImGuizmo.h"
#include "ContentBrowser.h"
#include "Project.h"
#include "EditorSettings.h"
#include "NavMeshBuilder.h"
#include <optional>
#include <unordered_map>

// Game и сцена живут дольше редактора; редактор владеет только UI и viewport.
class Editor {
public:
	Editor(Game& game, const std::string& draftsDirectory);
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
	// Жизненный цикл приложения, раскладка и пользовательские настройки.
	static LRESULT CALLBACK HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
	void DrawDockspace();
	void DrawProjectDialogs();
	void DrawSettings();
	EditorSettings settings, settingsDraft;
	std::string settingsError;
	bool openSettings = false;
	int capturingBinding = -1, bindingCaptureFrame = 0;
	void QueueProjectOpen(const std::string& file);
	void ApplyProjectAction();
	void RequestClose();
	void HandleShortcuts();
	// Документы сцены и объекта: загрузка, сохранение и отложенные действия.
	void ApplyDocumentActions();
	void ContinuePendingAction();
	bool SaveScene(Project& destination, std::string& error);
	bool CaptureScene(std::string& snapshot, std::string& error) const;
	void RefreshSceneDirty();
	enum class PendingAction { None, Close, OpenProject, NewProject, OpenObject, ReturnScene, OpenScene, Play };
	PendingAction pendingAction = PendingAction::None;
	bool checkPendingAction = false, proceedPendingAction = false;
	bool showUnsavedDialog = false, closeUnsavedDialog = false;
	bool resumeAfterSave = false, requestedSave = false, saveShortcut = false;
	bool scenePanelFocused = false, checkSceneDirty = false, sceneDirty = true;
	std::string savedScene, documentError;
	std::string sceneFile, pendingSceneFile;
	bool requestedSceneOpen = false, openSceneAfterReturn = false;
	void QueueSceneOpen(const std::string& file);
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
	// Иерархия и инспектор используют общее выделение и команды сцены.
	void DrawTreeInspector();
	void ApplyTreeAction();
	enum class TreeOperation { None, Add, Rename, Remove, Reparent, MoveComponent, AddComponent, RemoveComponent, Inheritance };
	// Порядок совпадает с подписями меню; виды начиная с Collider доступны только на корне.
	enum class ComponentKind { Mesh, DirectionalLight, PointLight, GameCamera, LuaScript, NavMeshVolume, Timer, Collider, StaticBody, DynamicBody, KinematicBody, CharacterMovement, NavAgent, BoxTrigger, UtilityAI };
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
	void DrawHierarchy();
	enum class ObjectType { None, Empty, Cube, Sphere, Plane, DirectionalLight, PointLight, Mesh, NavMeshVolume };
	void DrawCreateMenu();
	void ApplySceneActions();
	void DrawObjectDropTarget(const ImVec2& minimum, const ImVec2& maximum, bool inViewport = false);
	struct ObjectInsertRequest {
		std::string path;
		DXVector3 position = DXVector3::Zero;
	} requestedObjectInsert;
	struct ViewportRay {
		DXVector3 origin, direction;
		float length = 0;
	};
	bool BuildViewportRay(const ImVec2& origin, const ImVec2& size, ViewportRay& ray) const;
	bool GetObjectDropPosition(const ImVec2& origin, const ImVec2& size, DXVector3& position) const;
	bool CanDeleteObject(GameObject* object) const;
	ObjectType requestedCreate = ObjectType::None;
	GameObject* requestedDelete = nullptr;
	GameObject* requestedDuplicate = nullptr;
	std::string sceneActionError;
	void DrawTransformTree(TransformNode& node);
	void DrawInspector();
	void DrawObjectInspector();
	void OpenTexturePreview(const std::string& file);
	void CloseTexturePreview();
	void CloseAssetEditor();
	void OpenMaterialEditor(const std::string& file);
	void CloseMaterialEditor();
	void DrawMaterialInspector();
	bool ApplyMaterialSettings();
	bool MaterialSettingsDirty() const;
	void RecordMaterialSettings(const MaterialData& before, const MaterialData& after, const char* name);
	void TrackMaterialEdit(const MaterialData& before, const char* name, bool changed);
	void FinishMaterialEdit();
	void RevertMaterialSettings();
	bool AssetSettingsDirty() const;
	void DrawTextureInspector();
	void RefreshTexturePreview(TextureResource& resource);
	bool ApplyTextureSettings();
	bool TextureSettingsDirty() const;
	void RecordTextureSettings(const TextureSettings& before, const char* name);
	void RevertTextureSettings();
	void DrawAssetDialogs();
	void ApplyAssetActions();
	enum class AssetAction { None, OpenTexture, OpenMaterial, OpenUtilityTable, OpenUtilityCurve, OpenUtilityCurveInTable, CloseTexture,
		CloseMaterial, CloseUtilityTable, CloseUtilityCurve, HideInspector, ContinueDocument, ContinueSave };
	enum class AssetTab { None, Texture, Material, UtilityTable, UtilityCurve };
	void OpenUtilityAsset(const std::string& file);
	void DrawUtilityTable();
	void DrawUtilityTableContents();
	void ShowUtilityCurve(bool inTable);
	void DrawUtilityParameters(UtilityTableData& data);
	void DrawUtilityActions(UtilityTableData& data);
	void DrawUtilityCurve();
	void DrawUtilitySaveControls(AssetTab tab, bool dirty);
	bool ApplyUtilityAsset(AssetTab tab);
	void RevertUtilityAsset(AssetTab tab);
	bool InspectorAssetSettingsDirty() const;
	template<class Data> struct UtilityAssetDraft {
		std::string file, error;
		Data saved, draft;
		bool ready = false;
		std::vector<Data> history;
		size_t historyPosition = 0;
		ImGuiID editingItem = 0;
		void Open(std::string path) {
			*this = {};
			file = std::move(path);
			ready = saved.Read(file, error);
			draft = saved;
			if (ready) history.push_back(draft);
		}
		bool Dirty() const { return ready && draft != saved; }
		bool Save() {
			if (!ready || !draft.Save(file, error)) return false;
			saved = draft;
			return true;
		}
		void Revert() {
			FinishEdit();
			draft = saved;
			RecordSnapshot(draft);
			error.clear();
		}
		void RecordEdit(const Data& before, ImGuiID activeItem) {
			// При смене виджета завершаем предыдущую команду состоянием до нового ввода.
			if (editingItem && editingItem != activeItem) {
				RecordSnapshot(before);
				editingItem = 0;
			}
			if (draft == before) return;
			if (activeItem) editingItem = activeItem;
			else RecordSnapshot(draft);
		}
		void FinishEdit() {
			if (!editingItem) return;
			RecordSnapshot(draft);
			editingItem = 0;
		}
		bool CanStep(int direction) const {
			if (history.empty()) return false;
			const bool pending = editingItem && draft != history[historyPosition];
			return direction < 0 ? pending || historyPosition > 0 : !pending && historyPosition + 1 < history.size();
		}
		void Step(int direction) {
			FinishEdit();
			if (!CanStep(direction)) return;
			if (direction < 0) --historyPosition;
			else ++historyPosition;
			draft = history[historyPosition];
			error.clear();
		}
	private:
		void RecordSnapshot(const Data& value) {
			if (history.empty() || value == history[historyPosition]) return;
			history.erase(history.begin() + historyPosition + 1, history.end());
			history.push_back(value);
			// Начальное состояние тоже занимает место в списке снимков.
			if (history.size() > 129) history.erase(history.begin());
			historyPosition = history.size() - 1;
		}
	};
	UtilityAssetDraft<UtilityTableData> utilityTable;
	UtilityAssetDraft<UtilityCurveData> utilityCurve;
	struct UtilityCurveCheck {
		UtilityCurveData data;
		std::string error;
	};
	struct UtilityScriptCheck {
		std::string resolved, error;
	};
	struct UtilityActionPreview {
		std::vector<std::optional<double>> contributions;
		std::optional<double> score;
	};
	bool utilityScorePreview = false;
	std::vector<std::optional<double>> utilityPreviewInputs;
	std::unordered_map<std::string, UtilityCurveCheck> utilityCurveChecks;
	std::unordered_map<std::string, UtilityScriptCheck> utilityScriptChecks;
	const UtilityCurveCheck& CheckUtilityCurve(const std::string& path);
	const UtilityScriptCheck& CheckUtilityScript(const std::string& path);
	void ClearUtilityAssetChecks();
	std::vector<UtilityActionPreview> EvaluateUtilityPreview(const UtilityTableData& data);
	struct CurvePointDrag {
		size_t index = 0;
		UtilityCurveData::Point start{};
		double left = 0, right = 0;
		bool active = false;
	} curvePointDrag;
	// Диапазон отображения относится к открытому редактору, а не к ресурсу кривой.
	double utilityCurveXMin = 0, utilityCurveXMax = 1;
	void DrawUtilityCurveGraph(UtilityCurveData& data);
	ImGuiID viewportDockId = 0;
	size_t utilityParameterPage = 0;
	bool focusUtilityTable = false;
	bool utilityTableDetached = false, utilityCurveInTable = false;
	enum class UtilityTableDock { None, Detach, Attach };
	UtilityTableDock utilityTableDock = UtilityTableDock::None;
	void RequestAssetAction(AssetAction action, const std::string& file = {});
	void ContinueAssetAction();
	AssetTab PendingAssetConfirmation() const;
	AssetAction assetAction = AssetAction::None;
	AssetTab focusAssetTab = AssetTab::None;
	AssetTab requestedAssetApply = AssetTab::None;
	AssetTab assetPrompt = AssetTab::None;
	AssetTab focusedAssetTab = AssetTab::None, historyAssetTab = AssetTab::None;
	TextureSettings textureSettings, textureSettingsDraft;
	MaterialData materialSettings, materialDraft;
	MaterialData materialEditBefore, materialEditAfter;
	ImGuiID materialEditId = 0;
	const char* materialEditName = nullptr;
	bool showMaterialTab = false, materialReady = false;
	bool textureSettingsReady = false;
	bool showUnsavedAssetDialog = false, closeAssetDialog = false;
	std::string pendingAssetFile;
	std::string textureFile, textureSettingsError, textureError;
	std::string materialFile, materialSettingsError;
	// Собственная COM-ссылка переживает смену кеша сцены; освобождается явно редактором.
	ID3D11ShaderResourceView* texturePreviewView = nullptr;
	D3D11_TEXTURE2D_DESC textureDescription{};
	DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;
	bool showTextureTab = false, fitTexture = true;
	bool resetTextureScroll = false;
	float textureZoom = 100.0f;
	int textureBackground = 0;
	void DrawMeshInspector(MeshComponent& mesh);
	void DrawUtilityComponentInspector(UtilityAIComponent& component);
	enum class TransformField { Position, Rotation, Scale };
	static constexpr float MinimumScale = 0.001f;
	static bool IsValidEditorScale(const DXVector3& scale);
	void DrawTransformField(TransformField field, const char* label, bool fromGroupCenter = false);
	// Черновики ввода принадлежат UI; компонент получает только подтверждённые пути.
	struct MeshPathDraft {
		std::string mesh, material;
		std::string appliedMesh, appliedMaterial;
		std::string meshError, materialError;
	};
	std::unordered_map<MeshComponent*, MeshPathDraft> meshPathDrafts;
	// Навигация: генератор работает в фоне, результат применяется между кадрами.
	void DrawNavMeshVolumeInspector(NavMeshVolumeComponent& volume);
	void DrawCharacterMovementInspector(CharacterMovementComponent& character);
	void DrawNavAgentInspector(NavAgentComponent& agent);
	void ApplyNavigationActions();
	NavMeshBuilder navBuilder;
	// Итог генерации, путь данных относительно проекта и ошибка последнего ввода параметров.
	std::string navBuildMessage, navBuildPath, navSettingsError;
	bool requestedNavBuild = false, navStale = false;
	int navStaleCountdown = 0;
	// Изображение сцены, панели вьюпорта и операции гизмо.
	void DrawViewport();
	void DrawViewportToolbar();
	bool DrawSelectionTransformPanel(const ImVec2& origin, const ImVec2& size);
	bool groupFromCenter = true;
	void PickObject(const ImVec2& origin, const ImVec2& size, bool toggle = false, bool context = false);
	void SelectObject(GameObject* object, bool toggle = false);
	void SelectContextObject(GameObject* object);
	std::vector<GameObject*> SelectedObjects() const;
	bool CanDeleteSelection() const;
	void SelectNode(TransformNode* node);
	bool IsObjectSelected(GameObject* object) const;
	void DrawSelectionBounds(const ImVec2& origin, const ImVec2& size);
	void DrawGizmo(const ImVec2& origin, const ImVec2& size, bool blocked = false);
	bool ResizeViewport(UINT width, UINT height);
	void ReleaseViewport();
	const char* ObjectLabel(GameObject& object) const;
	CameraMovementComponent* GetCameraMovement() const;
	void ConfigureCameraNavigation(CameraMovementComponent& movement) const;

	void ClearDocumentHistory();
	// История документа. Сырые адреса допустимы только в текущем жесте, записи используют ID.
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
	enum class HistoryKind { Node, Component, Structure, Group, TextureSettings, MaterialSettings };
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
		TextureSettings textureBefore, textureAfter;
		MaterialData materialBefore, materialAfter;
		ObjectState objectBefore, objectAfter;
		HistoryId selectionBefore = 0, selectionAfter = 0, cameraBefore = 0, cameraAfter = 0;
		std::vector<HistoryId> selectedBefore, selectedAfter;
		std::vector<HistoryEntry> children;
	};
	struct DocumentHistory {
		std::vector<HistoryEntry> entries;
		size_t position = 0;
		std::vector<ObjectState> identities;
	} documentHistory, parkedHistory, textureHistory, materialHistory;
	AssetTab ActiveHistoryAsset() const;
	const DocumentHistory& ActiveHistory() const;
	bool CanStepHistory(int direction) const;
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
	void PushHistory(HistoryEntry entry, DocumentHistory* destination = nullptr);
	void BeginProperty(HistoryKind kind, HistoryId target, const std::string& before, const char* name);
	void FinishProperty(const std::string* after = nullptr);
	void TrackComponentProperty(Component& component, const std::string& before, const char* name, bool changed);
	HistoryEntry pendingProperty;
	bool editingProperty = false;
	bool BeginStructure(GameObject* object, const char* name, bool creating = false);
	void FinishStructures();
	std::vector<HistoryEntry> pendingStructures;
	int requestedHistoryStep = 0;
	AssetTab requestedHistoryAsset = AssetTab::None;
	void ApplyHistoryStep();
	bool RestoreObjectState(const ObjectState& state, HistoryId target);
	bool RestoreObjectStates(std::vector<ObjectState> states);
	bool ApplyHistoryEntry(const HistoryEntry& entry, bool undo);
	void RestoreSelectedObjects(const std::vector<HistoryId>& objects, HistoryId active);
	void RestoreHistorySelection(HistoryId selection, HistoryId camera);
	TransformState CaptureTransform(GameObject* object, TransformNode* node) const;
	void TrackTransformEdit(const TransformState& before, bool fromGroupCenter = false);
	void FinishTransformEdit();
	void Undo();
	void Redo();
	TransformState pendingEdit;
	std::vector<TransformState> pendingGroup;
	void CaptureTransformGroup(const TransformState& before);
	bool ApplyTransformGroup(const DXMatrix& delta);
	DXMatrix TransformMatrix(const TransformState& state) const;
	DXVector3 SelectionCenter(const TransformState* before = nullptr) const;
	bool ApplyTransformGroupStep(const TransformState& before, bool fromGroupCenter = false);
	DXMatrix groupEditDelta;
	DXMatrix groupGizmoStart, groupGizmoCurrent;
	bool editingTransform = false;
	bool editingGizmo = false;

	// Заимствованный движок и принадлежащие редактору UI/GPU-ресурсы.
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
