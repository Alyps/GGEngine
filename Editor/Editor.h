#pragma once
#include <core.h>
#include "imgui.h"
#include "ImGuizmo.h"
#include "ContentBrowser.h"
#include <unordered_map>

// Game и сцена живут дольше редактора; редактор владеет только UI и viewport.
class Editor {
public:
	explicit Editor(Game& game, const std::string& resourceRoot);
	~Editor();
	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;
	bool Init();
	int Run();

private:
	static LRESULT CALLBACK HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
	void DrawDockspace();
	void DrawHierarchy();
	enum class ObjectType { None, Empty, Cube, Sphere, Plane, DirectionalLight, PointLight, Mesh };
	void DrawCreateMenu();
	void ApplySceneActions();
	bool CanDeleteObject(GameObject* object) const;
	ObjectType requestedCreate = ObjectType::None;
	GameObject* requestedDelete = nullptr;
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
