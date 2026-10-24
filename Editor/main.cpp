#include <core.h>
#include "PhysicsTestScene.h"
#include <cstring>


int main(int argc, char** argv) {
	if (argc > 1 && std::strcmp(argv[1], "--physics-test") == 0) {
		PhysicsTestScene scene;
		return scene.Run();
	}
	Game game(L"Editor - white cube test");
	Level testLevel;
	GameObject cube;
	MeshComponent cubeMesh;

	cubeMesh.bind_to_node(&cube.fetTransform());
	cube.addComponent(cubeMesh);
	testLevel.addObject(&cube);
	game.setStartLevel(&testLevel);

	game.Init();
	return game.Run();
}
