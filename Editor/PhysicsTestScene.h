#pragma once
#include "core.h"
#include "StandartResources/PhysicsComponents.h"

// Game владеет уровнем; объекты и компоненты создаются через create.
class PhysicsTestScene {
    Game game;
    bool ready = false;
public:
    PhysicsTestScene();
    int Run();
};
