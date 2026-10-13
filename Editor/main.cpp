#include <core.h>


int main() {
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
