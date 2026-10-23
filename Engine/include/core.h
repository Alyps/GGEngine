#pragma once

#include <string>
#include "Window.h"
#include "RenderServer.h"
#include "GGTime.h"
#include "InputDevice.h"
#include "Object.h"

class Game;

class Level {
private:
	std::vector<GameObject*> objects;
	std::vector<GameObject*> pendingObjects;
	bool shuttingDown = false;


public:
	Level() = default;
	~Level();
	Level(const Level&) = delete;
	Level& operator=(const Level&) = delete;
	void Init(Game& game);
	void Update(Game& game, double deltaTime);

	// При успехе уровень принимает владение объектом, созданным через new.
	bool addObject(GameObject* obj);
	GameObject* createObject();
	void destroyObject(GameObject* obj);
	void applyPendingChanges(Game& game);
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

	// Game принимает владение стартовым уровнем до Init.
	void setStartLevel(Level* level) {
		if (current_level || start_level == level) return;
		delete start_level;
		start_level = level;
	}
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

	// Owning pointers: allocate and release explicitly in core.cpp.
	// Clean up partially created systems if construction throws.
	// Before destroying inputDevice, disconnect it from windowContext.
	DisplayWindow32* window = nullptr;
	InputDevice* inputDevice = nullptr;
	RenderServer* renderServer = nullptr;




};
