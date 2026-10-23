#include <core.h>
#include "StandartResources/RenderPresets.h"
#include "ResourceManager.h"


int main() {
	// Менеджер уничтожается после Game, сохраняя материал до завершения рендера.
	ResourceManager resources;
	Game game(L"Editor - white cube test");
	Level testLevel;
	GameObject cube;
	MeshComponent cubeMesh;
	GameObject floor;
	MeshComponent floorMesh(MeshPresets::CreateQuad(5.0f, 7.0f));
	GameObject secondCube;
	// Левый куб использует Default.material.json; центральный — CreateSimple.
	MeshComponent secondCubeMesh(resources);
	GameObject sphere;
	MeshComponent sphereMesh(MeshPresets::CreateCircle(0.5f));
	GameObject cameraObject;
	EditorCamera camera;
	CameraMovementComponent cameraMovement;
	GameObject directionalObject;
	DirectionalLightComponent directional;
	GameObject pointObject;
	PointLightComponent point;

	// Поворот -45° по yaw и -35.264° по pitch направляет лучи вдоль (1, -1, -1).
	directionalObject.fetTransform().set_rotation(
		DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0.0f));
	directional.color = { 0.0f, 0.0f, 1.0f };
	directional.intensity = 0.6f;
	directional.bind_to_node(&directionalObject.fetTransform());
	directionalObject.addComponent(directional);
	testLevel.addObject(&directionalObject);

	// Зелёный источник освещает верхний правый задний угол куба.
	pointObject.fetTransform().set_position(DXVector3(2.0f, 2.0f, -2.0f));
	point.color = { 0.0f, 1.0f, 0.0f };
	point.intensity = 12.0f;
	point.range = 5.0f;
	point.bind_to_node(&pointObject.fetTransform());
	pointObject.addComponent(point);
	testLevel.addObject(&pointObject);

	// Начальная ориентация задана кватернионом, камера смотрит на куб вдоль -Z.
	cameraObject.fetTransform().set_position(DXVector3(0.0f, 0.0f, 3.0f));
	cameraObject.fetTransform().set_rotation(DXQuaternion::Identity);
	camera.bind_to_node(&cameraObject.fetTransform());
	cameraMovement.bind_to_node(&cameraObject.fetTransform());

	// Оба компонента используют одну ветку дерева объекта.
	cameraObject.addComponent(camera);
	cameraObject.addComponent(cameraMovement);
	testLevel.addObject(&cameraObject);

	cubeMesh.bind_to_node(&cube.fetTransform());
	cube.addComponent(cubeMesh);
	testLevel.addObject(&cube);

	// Плоскость XY поворачиваем в пол XZ, лицевая нормаль смотрит вверх.
	floor.fetTransform().set_position(DXVector3(0.0f, -0.5f, 0.0f));
	floor.fetTransform().set_rotation(
		DXQuaternion::CreateFromAxisAngle(DXVector3::UnitX, -DirectX::XM_PIDIV2));
	floorMesh.bind_to_node(&floor.fetTransform());
	floor.addComponent(floorMesh);
	testLevel.addObject(&floor);

	// Дополнительный куб стоит слева, его нижняя грань совпадает с полом.
	secondCube.fetTransform().set_position(DXVector3(-1.5f, 0.0f, 0.0f));
	secondCubeMesh.bind_to_node(&secondCube.fetTransform());
	secondCube.addComponent(secondCubeMesh);
	testLevel.addObject(&secondCube);

	// Шар того же диаметра стоит справа от исходного куба.
	sphere.fetTransform().set_position(DXVector3(1.5f, 0.0f, 0.0f));
	sphereMesh.bind_to_node(&sphere.fetTransform());
	sphere.addComponent(sphereMesh);
	testLevel.addObject(&sphere);
	game.setStartLevel(&testLevel);

	game.Init();
	// Общий фон сохраняет видимость граней, на которые не падает свет.
	game.getRenderServer().setAmbientColor({ 0.12f, 0.12f, 0.12f });
	game.getRenderServer().setCamera(&camera);
	const int result = game.Run();
	// Снимаем невладеющую ссылку до уничтожения локальных компонентов.
	game.getRenderServer().setCamera(nullptr);
	return result;
}
