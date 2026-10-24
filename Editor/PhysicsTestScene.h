#pragma once
#include "core.h"
#include "Physics/PhysicsComponents.h"
#include "PhysicsTestView.h"

// Level/GameObject keep non-owning pointers. Declaration order keeps all referents alive.
class PhysicsTestScene {
    Game game;
    GameObject floor;
    GameObject cube;
    GameObject marker;
    PhysicsTestBoxComponent floorMesh;
    PhysicsTestBoxComponent cubeMesh;
    PhysicsTestBoxComponent markerMesh;
    BoxColliderComponent floorCollider;
    BoxColliderComponent cubeCollider;
    StaticRigidBodyComponent floorBody;
    DynamicRigidBodyComponent cubeBody;
    Level level;
public:
    PhysicsTestScene();
    int Run();
};
