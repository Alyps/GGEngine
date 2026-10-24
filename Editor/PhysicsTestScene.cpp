#include "PhysicsTestScene.h"
#include "PhysicsTestView.h"
#include "Editor.h"

PhysicsTestScene::PhysicsTestScene()
    : game(L"PhysicsTestScene - PhysX system")
{
    auto* level = new (std::nothrow) Level;
    if (!level) return;
    game.setStartLevel(level);

    auto* floor = level->createObject();
    auto* cube = level->createObject();
    auto* marker = level->createObject();
    if (!floor || !cube || !marker) return;
    // Коллайдеры задаются отдельно от видимой геометрии.
    floor->getTransform().set_position({0, -0.2f, 0});
    floor->getTransform().set_scale({8, 0.4f, 8});
    cube->getTransform().set_position({-1, 5, 0});
    cube->getTransform().set_rotation(DXQuaternion::CreateFromAxisAngle(DXVector3::UnitY, 0.45f));
    marker->getTransform().set_position({2, 1, -1});
    marker->getTransform().set_scale({0.7f, 0.7f, 0.7f});

    auto* floorMesh = floor->createComponent<MeshComponent>(PhysicsTestBoxMesh(DXColor(0.40f, 0.53f, 0.68f)));
    auto* cubeMesh = cube->createComponent<MeshComponent>(PhysicsTestBoxMesh(DXColor(1, 0.52f, 0.12f)));
    auto* markerMesh = marker->createComponent<MeshComponent>(PhysicsTestBoxMesh(DXColor(0.15f, 0.85f, 0.85f)));
    if (!floorMesh || !cubeMesh || !markerMesh) return;
    floorMesh->bind_to_node(&floor->getTransform());
    cubeMesh->bind_to_node(&cube->getTransform());
    markerMesh->bind_to_node(&marker->getTransform());

    // Локальные указатели заимствованы: объект уже владеет созданным компонентом.
    if (!floor->createComponent<BoxColliderComponent>() || !floor->createComponent<StaticRigidBodyComponent>()) return;
    if (!cube->createComponent<DynamicRigidBodyComponent>(2.0f) || !cube->createComponent<BoxColliderComponent>()) return;
    if (!ConfigurePhysicsTestCamera(game, *level)) return;

    auto* lightObject = level->createObject();
    auto* light = lightObject ? lightObject->createComponent<DirectionalLightComponent>() : nullptr;
    if (!light) return;
    // Камера, свет и материалы используют архитектуру Develop без обхода рендера.
    lightObject->getTransform().set_rotation(DXQuaternion::CreateFromYawPitchRoll(-DirectX::XM_PIDIV4, -0.6154797f, 0));
    light->bind_to_node(&lightObject->getTransform());
    light->color = {1, 0.96f, 0.88f};
    light->intensity = 1.5f;
    game.getRenderServer().setAmbientColor({0.25f, 0.25f, 0.25f});
    ready = true;
}

int PhysicsTestScene::Run()
{
    if (!ready) { std::cerr << "Cannot create physics test scene\n"; return 1; }
    game.Init();
    if (!game.getPhysicsServer().getLastError().empty()) return 1;
    // Та же оболочка редактора, что у обычной сцены Develop.
    Editor editor(game);
    return editor.Run();
}
