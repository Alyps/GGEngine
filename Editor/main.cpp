#include <core.h>
#include "StandartResources/RenderPresets.h"
#include "ResourceManager.h"
#include <vector>

namespace {
// Компонент проверки живёт в исходной сцене и не входит в список удаляемых кубов.
class SpawnObjectsComponent : public Component {
    Level& level;
    ResourceManager& resources;
    // Объектами владеет уровень; здесь сохраняем только адреса для удаления.
    std::vector<GameObject*> spawnedObjects;
    bool spaceWasDown = false;
    bool escapeWasDown = false;

public:
    SpawnObjectsComponent(Level& level, ResourceManager& resources)
        : level(level), resources(resources) {}
    void Init(Game& game) override {}

    void Update(Game& game, double) override {
        auto& input = game.getInputDevice();
        const bool spaceDown = input.IsKeyDown(Keys::Space);
        const bool escapeDown = input.IsKeyDown(Keys::Escape);
        // Реагируем на переход в нажатое состояние, а не на удержание клавиши.
        const bool spawn = spaceDown && !spaceWasDown;
        const bool clear = escapeDown && !escapeWasDown;
        spaceWasDown = spaceDown;
        escapeWasDown = escapeDown;
        if (GetForegroundWindow() != game.getWindow().getWindow()) return;

        // При одновременном нажатии приоритет у очистки. Исходная сцена остаётся.
        if (clear) {
            for (GameObject* object : spawnedObjects) level.destroyObject(object);
            spawnedObjects.clear();
            return;
        }
        if (!spawn) return;

        GameObject* object = level.createObject();
        if (!object) return;
        auto* mesh = object->createComponent<MeshComponent>(resources);
        // Не оставляем пустой объект при неудачном создании компонента.
        if (!mesh) {
            level.destroyObject(object);
            return;
        }

        // Небольшие кубы раскладываем рядами над исходной сценой.
        const size_t index = spawnedObjects.size();
        object->getTransform().set_position(DXVector3(
            -1.2f + static_cast<float>(index % 5) * 0.6f,
            0.8f, -1.5f - static_cast<float>(index / 5) * 0.6f));
        object->getTransform().set_scale(DXVector3(0.4f, 0.4f, 0.4f));
        mesh->bind_to_node(&object->getTransform());
        spawnedObjects.push_back(object);
    }
};
}

int main() {
	// Менеджер уничтожается после Game, сохраняя материал до завершения рендера.
	ResourceManager resources;
	Game game(L"Editor - white cube test");
	Level& testLevel = *new Level();
	game.setStartLevel(&testLevel);
	// Пробел создаёт куб; Esc запрашивает удаление всех созданных здесь кубов.
	GameObject* spawnController = testLevel.createObject();
	if (spawnController) spawnController->createComponent<SpawnObjectsComponent>(testLevel, resources);
	GameObject& cube = *testLevel.createObject();
	MeshComponent& cubeMesh = *cube.createComponent<MeshComponent>();
	GameObject& floor = *testLevel.createObject();
	MeshComponent& floorMesh = *floor.createComponent<MeshComponent>(MeshPresets::CreateQuad(5.0f, 7.0f));
	GameObject& secondCube = *testLevel.createObject();
	// Левый куб использует Default.material.json; центральный — CreateSimple.
	MeshComponent& secondCubeMesh = *secondCube.createComponent<MeshComponent>(resources);
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

	// Оба компонента используют одну ветку дерева объекта.

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
	return game.Run();
}
