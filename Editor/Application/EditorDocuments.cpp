#include "Editor.h"
#include "imgui_internal.h"
#include "EditorFileUtils.h"
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
using EditorFiles::DiskPath;
using EditorFiles::PickProjectPath;
void Editor::QueueProjectOpen(const std::string& file)
{
	if (editedObject) { documentError = "Return to Scene before switching projects."; return; }
	if (!Project::Open(file, pendingProject, projectError)) return;
	pendingAction = PendingAction::OpenProject;
	checkPendingAction = true;
}

void Editor::DrawProjectDialogs()
{
	if (showNewProject) {
		showNewProject = false; projectCreationCompleted = false;
		projectCreationError.clear(); projectCreationTarget = {};
		ImGui::OpenPopup("New project");
	}
	if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(creationSavesCurrentScene ? "Save the current project" : "Create a project with the default demo scene");
		if (ImGui::InputText("Name", newProjectName, sizeof(newProjectName))) { projectCreationError.clear(); projectCreationTarget = {}; }
		if (ImGui::InputText("Parent folder", newProjectParent, sizeof(newProjectParent))) { projectCreationError.clear(); projectCreationTarget = {}; }
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			std::string folder;
			projectCreationError.clear();
			if (PickProjectPath(game.getWindow().getWindow(), true, folder, projectCreationError)) {
				projectCreationTarget = {};
				if (folder.size() < sizeof(newProjectParent)) strcpy_s(newProjectParent, folder.c_str());
				else projectCreationError = "Selected folder path is too long.";
			}
			game.getTimeManager().reset();
		}
		ImGui::TextDisabled("A new folder with the project name will be created here.");
		ImGui::TextWrapped("%s", creationSavesCurrentScene
			? "The current scene and resource files will be saved in the selected location."
			: "A fresh demo scene will be saved. Current objects and resources will not be copied.");
		if (!projectCreationError.empty()) ImGui::TextWrapped("%s", projectCreationError.c_str());
		ImGui::BeginDisabled(projectCreationCompleted || requestedProjectCreation);
		if (ImGui::Button(projectCreationTarget.filePath.empty() ? (creationSavesCurrentScene ? "Save" : "Create") : "Retry saving scene")) requestedProjectCreation = true;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			resumeAfterSave = false; pendingAction = PendingAction::None;
			ImGui::CloseCurrentPopup();
		}
		if (projectCreationCompleted) { projectCreationCompleted = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if (showUnsavedDialog) { showUnsavedDialog = false; documentError.clear(); ImGui::OpenPopup("Unsaved scene"); }
	if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::BeginDisabled(closeUnsavedDialog);
		const char* next = pendingAction == PendingAction::Close ? "closing the editor"
			: pendingAction == PendingAction::NewProject ? "creating a new project"
			: pendingAction == PendingAction::ReturnScene ? "returning to the scene"
			: pendingAction == PendingAction::OpenScene ? "opening another scene"
			: pendingAction == PendingAction::OpenObject ? "opening another object" : "opening another project";
		ImGui::TextWrapped("Save the current %s before %s?", editedObject ? "object" : "scene", next);
		if (!documentError.empty()) ImGui::TextWrapped("%s", documentError.c_str());
		if (ImGui::Button("Save")) {
			resumeAfterSave = true;
			if (!editedObject && temporaryProject) { creationSavesCurrentScene = true; showNewProject = true; ImGui::CloseCurrentPopup(); }
			else requestedSave = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't save")) { proceedPendingAction = true; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			pendingAction = PendingAction::None; resumeAfterSave = closeAfterReturn = openSceneAfterReturn = false; ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		if (closeUnsavedDialog) { closeUnsavedDialog = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if ((!projectError.empty() || !documentError.empty()) && !ImGui::GetTopMostPopupModal()) {
		ImGui::Begin("Project message", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::TextWrapped("%s", projectError.c_str());
		ImGui::TextWrapped("%s", documentError.c_str());
		if (ImGui::Button("Dismiss")) { projectError.clear(); documentError.clear(); }
		ImGui::End();
	}
}

void Editor::RequestClose()
{
	// При закрытии проверяются оба документа: сначала объект, затем припаркованная сцена.
	closeAfterReturn = editedObject != nullptr;
	pendingAction = editedObject ? PendingAction::ReturnScene : PendingAction::Close;
	checkPendingAction = true;
	// Закрытие через панель задач должно показывать диалог и для свёрнутого окна.
	if (IsIconic(game.getWindow().getWindow())) ShowWindow(game.getWindow().getWindow(), SW_RESTORE);
}

bool Editor::CaptureScene(std::string& snapshot, std::string& error) const
{
	Level* level = game.getLevel();
	if (!level) { error = "No scene is open."; return false; }
	rapidjson::Document document;
	auto json = editedObject ? editedObject->Serialize(document.GetAllocator(), error) : level->Serialize(document.GetAllocator(), error);
	if (json.IsNull()) return false;
	rapidjson::StringBuffer bytes;
	rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);
	if (!json.Accept(writer)) { error = "Cannot serialize the current scene."; return false; }
	snapshot.assign(bytes.GetString(), bytes.GetSize());
	return true;
}

void Editor::RefreshSceneDirty()
{
	checkSceneDirty = false;
	std::string snapshot, error;
	sceneDirty = !CaptureScene(snapshot, error) || savedScene.empty() || snapshot != savedScene;
}

bool Editor::SaveScene(Project& destination, std::string& error)
{
	std::string snapshot;
	if (!CaptureScene(snapshot, error)) return false;
	Project saved = destination;
	const std::string activeScene = !sceneFile.empty() ? sceneFile
		: !saved.startupScene.empty() ? saved.startupScene : saved.resourceDirectory + "/Scenes/Main.level.json";
	if (saved.startupScene.empty()) saved.startupScene = activeScene;
	// Открытие другого уровня не меняет стартовую сцену проекта; Ctrl+S пишет текущий документ.
	const auto path = (DiskPath(saved.Root()) / DiskPath(activeScene)).lexically_normal();
	std::error_code code;
	std::filesystem::create_directories(path.parent_path(), code);
	if (code) { error = "Cannot create scene directory: " + code.message(); return false; }
	const auto utf8 = path.generic_u8string();
	const std::string scenePath(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	// Манифест ссылается на сцену только после успешной атомарной записи самого уровня.
	if (!game.getLevel()->Save(scenePath, error) || !saved.Save(error)) return false;
	destination = std::move(saved);
	sceneFile = activeScene;
	savedScene = std::move(snapshot);
	sceneDirty = checkSceneDirty = false;
	return true;
}

void Editor::ContinuePendingAction()
{
	const PendingAction action = pendingAction;
	pendingAction = PendingAction::None;
	resumeAfterSave = false;
	if (action == PendingAction::Close) PostQuitMessage(0);
	else if (action == PendingAction::OpenProject) requestedProjectSwitch = true;
	else if (action == PendingAction::NewProject) { creationSavesCurrentScene = false; showNewProject = true; }
	else if (action == PendingAction::OpenObject) requestedObjectOpen = true;
	else if (action == PendingAction::ReturnScene) requestedSceneReturn = true;
	else if (action == PendingAction::OpenScene) requestedSceneOpen = true;
}

void Editor::ApplyDocumentActions()
{
	// Снимки снимаются между кадрами, после применения создания и удаления объектов.
	if (checkSceneDirty || checkPendingAction) RefreshSceneDirty();
	if (checkPendingAction) {
		checkPendingAction = false;
		if (game.getLevel() && sceneDirty) showUnsavedDialog = true;
		else ContinuePendingAction();
	}
	if (proceedPendingAction) { proceedPendingAction = false; ContinuePendingAction(); }
	if (!requestedSave) return;
	requestedSave = false;
	FinishTransformEdit(); FinishProperty();
	if (editedObject) {
		if (SaveObject(documentError) && resumeAfterSave) { closeUnsavedDialog = true; ContinuePendingAction(); }
		game.getTimeManager().reset();
		return;
	}
	if (temporaryProject || project.filePath.empty()) { creationSavesCurrentScene = true; showNewProject = true; return; }
	if (SaveScene(project, documentError)) {
		contentBrowser.RefreshFiles();
		if (resumeAfterSave) { closeUnsavedDialog = true; ContinuePendingAction(); }
	}
	game.getTimeManager().reset();
}

void Editor::ApplyProjectAction()
{
	const bool createDemo = initializeDemo;
	initializeDemo = false;
	if (requestedProjectCreation) {
		requestedProjectCreation = false;
		const Project* source = creationSavesCurrentScene && !project.filePath.empty() ? &project : nullptr;
		if (projectCreationTarget.filePath.empty()
			&& !Project::Create(newProjectParent, newProjectName, projectCreationTarget, projectCreationError, source)) return;
		if (!creationSavesCurrentScene) {
			// Новый уровень ещё не принадлежит Game: создаём и сохраняем демо без Init.
			auto* demo = new (std::nothrow) Level();
			GameObject* cameraObject = demo ? demo->createObject() : nullptr;
			auto* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
			auto* navigation = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
			if (!camera || !navigation || !Project::PopulateDemoScene(*demo)) {
				delete demo; projectCreationError = "Cannot allocate the demo scene."; return;
			}
			cameraObject->getTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
			camera->bind_to_node(&cameraObject->getTransform());
			navigation->bind_to_node(&cameraObject->getTransform());
			Project ready = projectCreationTarget;
			ready.startupScene = ready.resourceDirectory + "/Scenes/Main.level.json";
			const auto path = (DiskPath(ready.Root()) / DiskPath(ready.startupScene)).generic_u8string();
			const std::string scenePath(reinterpret_cast<const char*>(path.data()), path.size());
			const bool saved = demo->Save(scenePath, projectCreationError) && ready.Save(projectCreationError);
			delete demo;
			game.getTimeManager().reset();
			if (!saved) return;
			pendingProject = std::move(ready);
			requestedProjectSwitch = true;
			projectCreationCompleted = true;
		} else {
			const bool complete = SaveScene(projectCreationTarget, projectCreationError);
			// Время копирования файлов не должно превращаться в большой шаг симуляции.
			game.getTimeManager().reset();
			if (!complete) return;
			// Сохранение текущей работы сохраняет объекты, выбор и историю Undo.
			game.getResourceManager().SetProjectDirectory(projectCreationTarget.Root());
			project = std::move(projectCreationTarget);
			temporaryProject = false;
			contentBrowser = ContentBrowser(project.ResourceRoot(), project.Root());
			recentProjects.Remember(project.filePath, projectError);
			projectCreationCompleted = true;
			if (resumeAfterSave) ContinuePendingAction();
			return;
		}
	}
	if (!requestedProjectSwitch) return;
	requestedProjectSwitch = false;
	// До передачи Game уровень и камера принадлежат этой операции.
	Level* level = nullptr;
	if (!pendingProject.startupScene.empty()) {
		rapidjson::Document json;
		const auto path = (DiskPath(pendingProject.Root()) / DiskPath(pendingProject.startupScene)).generic_u8string();
		const std::string scenePath(reinterpret_cast<const char*>(path.data()), path.size());
		if (!Deserialization::LoadFile(scenePath, json, projectError)) return;
		level = Level::Deserialize(json, game.getComponentFactoryMap(), projectError);
		if (!level) return;
	} else level = new (std::nothrow) Level();
	if (!level) { projectError = "Cannot allocate scene."; return; }
	GameObject* cameraObject = pendingProject.startupScene.empty() ? level->createObject() : nullptr;
	EditorCamera* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
	CameraMovementComponent* movement = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
	if (cameraObject && (!camera || !movement)) { delete level; projectError = "Cannot allocate a project camera."; return; }
	if (pendingProject.startupScene.empty() && !cameraObject) { delete level; projectError = "Cannot allocate a project camera."; return; }
	if (createDemo && !Project::PopulateDemoScene(*level)) {
		delete level;
		projectError = "Cannot allocate the demonstration scene.";
		return;
	}
	if (cameraObject) {
		cameraObject->getTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
		camera->bind_to_node(&cameraObject->getTransform());
		movement->bind_to_node(&cameraObject->getTransform());
	}
	// Сбрасываем заимствованные адреса до Shutdown и удаления прежней сцены.
	FinishTransformEdit(); FinishProperty();
	CaptureDocumentIdentities(documentHistory);
	auto previousHistory = std::move(documentHistory);
	ResetSceneReferences();
	sceneActionError.clear();
	if (!game.replaceLevel(level, pendingProject.Root())) {
		delete level;
		documentHistory = std::move(previousHistory); RestoreDocumentIdentities(documentHistory);
		projectError = "Cannot initialize project systems. The previous project remains open.";
		return;
	}
	// После Init общий индекс содержит и компоненты загруженного дерева трансформаций.
	for (auto* object : level->getObjects())
		for (auto* component : object->getComponents()) {
			if (!camera) camera = dynamic_cast<EditorCamera*>(component);
			if (auto* navigation = dynamic_cast<CameraMovementComponent*>(component)) {
				ConfigureCameraNavigation(*navigation);
			}
		}
	game.getRenderServer().setCamera(camera);
	game.getRenderServer().setAmbientColor({ 0.12f, 0.12f, 0.12f });
	project = std::move(pendingProject);
	sceneFile = project.startupScene;
	temporaryProject = createDemo;
	savedScene.clear();
	if (!project.startupScene.empty()) CaptureScene(savedScene, documentError);
	sceneDirty = savedScene.empty();
	contentBrowser = ContentBrowser(project.ResourceRoot(), project.Root());
	if (!temporaryProject) recentProjects.Remember(project.filePath, projectError);
}

void Editor::ResetSceneReferences()
{
	ClearDocumentHistory();
	selectedObjects.clear();
	selectedObject = nullptr; selectedNode = nameDraftNode = nullptr;
	meshPathDrafts.clear(); treeAction = {};
	requestedCreate = ObjectType::None; requestedDelete = requestedDuplicate = requestedObjectExport = nullptr;
	requestedObjectInsert.clear();
	navigating = false;
	ImGui::ClearActiveID(); ImGui::ClearDragDrop();
}

bool Editor::ReplaceEditorLevel(Level* level, EditorCamera* camera, std::string& error)
{
	FinishTransformEdit(); FinishProperty();
	CaptureDocumentIdentities(documentHistory);
	auto previousHistory = std::move(documentHistory);
	ResetSceneReferences();
	if (!game.replaceLevel(level, project.Root())) {
		documentHistory = std::move(previousHistory); RestoreDocumentIdentities(documentHistory);
		error = "Cannot initialize document preview."; return false;
	}
	for (auto* object : level->getObjects()) for (auto* component : object->getComponents()) {
		if (!camera) camera = dynamic_cast<EditorCamera*>(component);
		if (auto* movement = dynamic_cast<CameraMovementComponent*>(component)) {
			ConfigureCameraNavigation(*movement);
		}
	}
	game.getRenderServer().setCamera(camera);
	game.getRenderServer().setAmbientColor({ 0.25f, 0.25f, 0.25f });
	return true;
}

void Editor::QueueObjectOpen(const std::string& file)
{
	pendingObjectFile = file;
	if (editedObject) { pendingAction = PendingAction::OpenObject; checkPendingAction = true; }
	else requestedObjectOpen = true;
}

void Editor::QueueSceneOpen(const std::string& file)
{
	const auto relative = DiskPath(file).lexically_normal().lexically_relative(DiskPath(project.Root()));
	if (relative.empty() || relative.has_root_path()) { documentError = "Choose a scene inside this project."; return; }
	for (const auto& part : relative) if (part == "..") { documentError = "Choose a scene inside this project."; return; }
	const auto path = relative.generic_u8string();
	pendingSceneFile.assign(reinterpret_cast<const char*>(path.data()), path.size());
	// Из режима объекта возвращаемся через оба независимых диалога сохранения.
	openSceneAfterReturn = editedObject != nullptr;
	pendingAction = editedObject ? PendingAction::ReturnScene : PendingAction::OpenScene;
	checkPendingAction = true;
}

bool Editor::SaveObject(std::string& error)
{
	std::string snapshot;
	if (!editedObject || !CaptureScene(snapshot, error) || !editedObject->Save(objectFile, error)) return false;
	savedScene = std::move(snapshot); sceneDirty = checkSceneDirty = false;
	return true;
}

void Editor::ApplyObjectActions()
{
	if (requestedObjectExport) {
		auto* object = requestedObjectExport; requestedObjectExport = nullptr;
		if (!object->isPendingDestroy() && object->Save(exportObjectPath, documentError)) {
			if (object == editedObject) { objectFile = exportObjectPath; CaptureScene(savedScene, documentError); RefreshSceneDirty(); }
			contentBrowser.RefreshFiles();
		}
	}
	if (requestedSceneReturn) {
		requestedSceneReturn = false;
		rapidjson::Document json;
		json.Parse(parkedScene.data(), parkedScene.size());
		Level* level = json.HasParseError() ? nullptr : Level::Deserialize(json, game.getComponentFactoryMap(), documentError);
		if (!level) { if (documentError.empty()) documentError = "Cannot restore the scene."; closeAfterReturn = false; return; }
		GameObject* previous = editedObject; editedObject = nullptr;
		FinishTransformEdit(); FinishProperty();
		CaptureDocumentIdentities(documentHistory);
		auto previousHistory = std::move(documentHistory);
		if (!ReplaceEditorLevel(level, nullptr, documentError)) {
			editedObject = previous; delete level; closeAfterReturn = false;
			documentHistory = std::move(previousHistory); RestoreDocumentIdentities(documentHistory); return;
		}
		documentHistory = std::move(parkedHistory);
		RestoreDocumentIdentities(documentHistory);
		savedScene = std::move(parkedBaseline); parkedScene.clear(); objectFile.clear();
		RefreshSceneDirty();
		if (closeAfterReturn) { closeAfterReturn = openSceneAfterReturn = false; RequestClose(); }
		else if (openSceneAfterReturn) { openSceneAfterReturn = false; pendingAction = PendingAction::OpenScene; checkPendingAction = true; }
	}
	if (requestedSceneOpen) {
		requestedSceneOpen = false;
		if (editedObject) { documentError = "Return to Scene before opening another level."; return; }
		rapidjson::Document json;
		const auto path = (DiskPath(project.Root()) / DiskPath(pendingSceneFile)).generic_u8string();
		const std::string file(reinterpret_cast<const char*>(path.data()), path.size());
		if (!Deserialization::LoadFile(file, json, documentError)) return;
		Level* level = Level::Deserialize(json, game.getComponentFactoryMap(), documentError);
		if (!level) return;
		// Старый документ живёт до полной загрузки нового и успешной подготовки систем.
		if (!ReplaceEditorLevel(level, nullptr, documentError)) { delete level; return; }
		sceneFile = pendingSceneFile;
		savedScene.clear(); CaptureScene(savedScene, documentError);
		sceneDirty = savedScene.empty(); checkSceneDirty = false;
	}
	if (!requestedObjectOpen) return;
	requestedObjectOpen = false;
	rapidjson::Document json;
	if (!Deserialization::LoadFile(pendingObjectFile, json, documentError)) return;
	GameObject* object = GameObject::Deserialize(json, game.getComponentFactoryMap(), documentError);
	if (!object) return;
	std::string backup;
	if (!editedObject && !CaptureScene(backup, documentError)) { delete object; return; }
	// До Init снимаем эталон ассета; камера и свет предпросмотра в него не входят.
	rapidjson::Document baselineDocument;
	auto description = object->Serialize(baselineDocument.GetAllocator(), documentError);
	rapidjson::StringBuffer bytes; rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);
	if (description.IsNull() || !description.Accept(writer)) { delete object; return; }
	const std::string baseline(bytes.GetString(), bytes.GetSize());
	auto* preview = new (std::nothrow) Level();
	if (!preview) { delete object; documentError = "Cannot allocate object preview."; return; }
	if (!preview->addObject(object)) { delete object; delete preview; documentError = "Cannot attach object to preview."; return; }
	GameObject* cameraObject = preview->createObject();
	auto* camera = cameraObject ? cameraObject->createComponent<EditorCamera>() : nullptr;
	auto* movement = cameraObject ? cameraObject->createComponent<CameraMovementComponent>() : nullptr;
	GameObject* lightObject = preview->createObject();
	auto* light = lightObject ? lightObject->createComponent<DirectionalLightComponent>() : nullptr;
	if (!camera || !movement || !light) { delete preview; documentError = "Cannot allocate preview camera or light."; return; }
	cameraObject->getTransform().set_position(object->getTransform().get_position() + DXVector3(0, 0, 3));
	lightObject->getTransform().set_rotation(DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0));
	light->intensity = 1.0f;
	const bool entering = editedObject == nullptr;
	const std::string oldBaseline = savedScene;
	FinishTransformEdit(); FinishProperty();
	CaptureDocumentIdentities(documentHistory);
	auto previousHistory = std::move(documentHistory);
	GameObject* previous = editedObject; editedObject = object;
	if (!ReplaceEditorLevel(preview, camera, documentError)) {
		editedObject = previous; delete preview;
		documentHistory = std::move(previousHistory); RestoreDocumentIdentities(documentHistory); return;
	}
	if (entering) {
		parkedScene = std::move(backup); parkedBaseline = oldBaseline;
		parkedHistory = std::move(previousHistory);
	}
	objectFile = std::move(pendingObjectFile); savedScene = baseline;
	SelectObject(object);
	RefreshSceneDirty();
	game.getTimeManager().reset();
}
