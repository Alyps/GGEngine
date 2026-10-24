#include "PhysicsTestScene.h"

PhysicsTestScene::PhysicsTestScene()
    : game(L"PhysicsTestScene - 3D falling cube"),
      floorMesh(DXColor(0.40f, 0.53f, 0.68f)),
      cubeMesh(DXColor(1.0f, 0.52f, 0.12f)),
      markerMesh(DXColor(0.15f, 0.85f, 0.85f)),
      floorCollider(DXVector3::One), cubeCollider(DXVector3::One),
      floorBody(floorCollider), cubeBody(cubeCollider, 2.0f)
{
    floor.fetTransform().set_position({0, -0.2f, 0});
    floor.fetTransform().set_scale({8, 0.4f, 8});
    cube.fetTransform().set_position({-1, 5, 0});
    cube.fetTransform().set_rotation(DXQuaternion::CreateFromAxisAngle(DXVector3::UnitY, 0.45f));
    marker.fetTransform().set_position({2, 1, -1});
    marker.fetTransform().set_scale({0.7f, 0.7f, 0.7f});
    marker.fetTransform().set_rotation(DXQuaternion::CreateFromAxisAngle(DXVector3::UnitY, -0.4f));

    const auto& window = game.getWindow();
    const DXMatrix viewProjection = PhysicsTestViewProjection(
        static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight()));
    floorMesh.SetViewProjection(viewProjection);
    cubeMesh.SetViewProjection(viewProjection);
    markerMesh.SetViewProjection(viewProjection);

    floorMesh.bind_to_node(&floor.fetTransform());
    cubeMesh.bind_to_node(&cube.fetTransform());
    markerMesh.bind_to_node(&marker.fetTransform());
    floorBody.bind_to_node(&floor.fetTransform());
    cubeBody.bind_to_node(&cube.fetTransform());
    floor.addComponent(floorMesh);
    floor.addComponent(floorCollider);
    floor.addComponent(floorBody);
    cube.addComponent(cubeMesh);
    cube.addComponent(cubeCollider);
    cube.addComponent(cubeBody);
    marker.addComponent(markerMesh);
    level.addObject(&floor);
    level.addObject(&cube);
    level.addObject(&marker);
    game.setStartLevel(&level);
}

int PhysicsTestScene::Run()
{
    if (!game.getPhysicsWorld().Init()) return 1;
    game.Init();
    return game.Run();
}
