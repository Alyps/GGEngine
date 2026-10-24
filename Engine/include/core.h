#pragma once

#include <string>
#include "Window.h"
#include "RenderServer.h"
#include "GGTime.h"
#include "InputDevice.h"
#include "Object.h"
#include "Physics/PhysicsWorld.h"

class Game;

class Level {
private:
	std::vector<GameObject*> objects;


public:
	void Init(Game& game);

	void addObject(GameObject* obj);
};

// Owns application systems. Gameplay belongs to a separate GameInstance.
class Game {
public:
	// On success, all systems are ready; initialization failures throw.
	explicit Game(std::wstring windowTitle, long clientWidth = 1280, long clientHeight = 720);
	// Define in core.cpp after including InputDevice.h.
	~Game() noexcept;

	Game(const Game&) = delete;
	Game& operator=(const Game&) = delete;
	Game(Game&&) = delete;
	Game& operator=(Game&&) = delete;

	DisplayWindow32& getWindow() noexcept { return *window; }
	const DisplayWindow32& getWindow() const noexcept { return *window; }

	InputDevice& getInputDevice() noexcept { return *inputDevice; }
	const InputDevice& getInputDevice() const noexcept { return *inputDevice; }

	RenderServer& getRenderServer() noexcept { return *renderServer; }
	const RenderServer& getRenderServer() const noexcept { return *renderServer; }

	TimeManager& getTimeManager() noexcept { return timeManager; }
	const TimeManager& getTimeManager() const noexcept { return timeManager; }

	PhysicsWorld& getPhysicsWorld() noexcept { return physicsWorld; }
	void reportInitializationFailure() noexcept { initializationFailed = true; }
	void registerRenderComponent(RenderComponent& component);
	void unregisterRenderComponent(RenderComponent& component);

	void setStartLevel(Level* level) { start_level = level; }
	Level* getLevel() { return current_level; }

	void Init();
	int Run();

	void InitCurrentLevel() { if (current_level) current_level->Init(*this); else std::cout << "No current level to initialize." << std::endl; }

private:

	Level* start_level = nullptr;
	Level* current_level = nullptr;

	// Its address is stored in HWND; keep it alive through window destruction.
	WindowMessageContext windowContext{};
	TimeManager timeManager;
	PhysicsWorld physicsWorld;
	RenderComponent* renderComponents = nullptr;
	bool initializationFailed = false;

	// Owning pointers: allocate and release explicitly in core.cpp.
	// Clean up partially created systems if construction throws.
	// Before destroying inputDevice, disconnect it from windowContext.
	DisplayWindow32* window = nullptr;
	InputDevice* inputDevice = nullptr;
	RenderServer* renderServer = nullptr;




};




