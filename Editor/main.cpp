#include <filesystem>
#include <core.h>
#include "StandartResources/RenderPresets.h"
#include "Editor.h"

int main() {
	// Берём AppData через Unicode API, независимо от рабочей папки процесса.
	const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
	if (!required) { std::cerr << "APPDATA is unavailable\n"; return 1; }
	std::wstring appData(required, L'\0');
	const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData.data(), required);
	if (!length || length >= required) { std::cerr << "Cannot read APPDATA\n"; return 1; }
	appData.resize(length);

	// Создаём всю цепочку каталогов; существующие файлы проекта сохраняются.
	const std::filesystem::path resourceDirectory = std::filesystem::path(appData) / L"GGEngine" / L"TestProject" / L"res";
	if (!resourceDirectory.is_absolute()) { std::cerr << "APPDATA must be absolute\n"; return 1; }
	std::error_code error;
	std::filesystem::create_directories(resourceDirectory, error);
	if (error) { std::cerr << "Cannot create resource directory: " << error.message() << '\n'; return 1; }
	// ContentBrowser по-прежнему получает обычную строку UTF-8 с разделителем /.
	const auto utf8Root = resourceDirectory.generic_u8string();
	const std::string resourceRoot(reinterpret_cast<const char*>(utf8Root.data()), utf8Root.size());
	Game game(L"GGEngine Editor");
	Level& testLevel = *new Level();
	game.setStartLevel(&testLevel);
	GameObject& cube = *testLevel.createObject();
	MeshComponent& cubeMesh = *cube.createComponent<MeshComponent>();
	GameObject& floor = *testLevel.createObject();
	MeshComponent& floorMesh = *floor.createComponent<MeshComponent>(MeshPresets::CreateQuad(5.0f, 7.0f));
	GameObject& secondCube = *testLevel.createObject();
	// Левый куб использует Default.material.json; центральный — CreateSimple.
	MeshComponent& secondCubeMesh = *secondCube.createComponent<MeshComponent>(true);
	GameObject& sphere = *testLevel.createObject();
	MeshComponent& sphereMesh = *sphere.createComponent<MeshComponent>(MeshPresets::CreateCircle(0.5f));
	GameObject& cameraObject = *testLevel.createObject();
	EditorCamera& camera = *cameraObject.createComponent<EditorCamera>();
	CameraMovementComponent& cameraMovement = *cameraObject.createComponent<CameraMovementComponent>();
	GameObject& directionalObject = *testLevel.createObject();
	DirectionalLightComponent& directional = *directionalObject.createComponent<DirectionalLightComponent>();
	GameObject& pointObject = *testLevel.createObject();
	PointLightComponent& point = *pointObject.createComponent<PointLightComponent>();

	// Поворот -45° по yaw и -35.264° по pitch направляет лучи вдоль (1, -1, -1).
	directionalObject.getTransform().set_rotation(
		DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0.0f));
	directional.color = { 0.0f, 0.0f, 1.0f };
	directional.intensity = 0.6f;
	directional.bind_to_node(&directionalObject.getTransform());

	// Зелёный источник освещает верхний правый задний угол куба.
	pointObject.getTransform().set_position(DXVector3(2.0f, 2.0f, -2.0f));
	point.color = { 0.0f, 1.0f, 0.0f };
	point.intensity = 12.0f;
	point.range = 5.0f;
	point.bind_to_node(&pointObject.getTransform());

	// Начальная ориентация задана кватернионом, камера смотрит на куб вдоль -Z.
	cameraObject.getTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
	cameraObject.getTransform().set_rotation(DXQuaternion::Identity);
	camera.bind_to_node(&cameraObject.getTransform());
	cameraMovement.bind_to_node(&cameraObject.getTransform());

	cubeMesh.bind_to_node(&cube.getTransform());

	// Плоскость XY поворачиваем в пол XZ, лицевая нормаль смотрит вверх.
	floor.getTransform().set_position(DXVector3(0.0f, -0.5f, 0.0f));
	floor.getTransform().set_rotation(
		DXQuaternion::CreateFromAxisAngle(DXVector3::UnitX, -DirectX::XM_PIDIV2));
	floorMesh.bind_to_node(&floor.getTransform());

	// Дополнительный куб стоит слева, его нижняя грань совпадает с полом.
	secondCube.getTransform().set_position(DXVector3(-1.5f, 0.0f, 0.0f));
	secondCubeMesh.bind_to_node(&secondCube.getTransform());

	// Шар того же диаметра стоит справа от исходного куба.
	sphere.getTransform().set_position(DXVector3(1.5f, 0.0f, 0.0f));
	sphereMesh.bind_to_node(&sphere.getTransform());

	game.Init();
	// Общий фон сохраняет видимость граней, на которые не падает свет.
	game.getRenderServer().setAmbientColor({ 0.12f, 0.12f, 0.12f });
	game.getRenderServer().setCamera(&camera);
	// Game освободит уровень и компоненты перед уничтожением своих систем.
	Editor editor(game, resourceRoot);
	return editor.Run();
}
