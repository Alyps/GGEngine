#pragma once

#include <string>
#include "Window.h"
#include "RenderServer.h"
#include "GGTime.h"
#include "InputDevice.h"
#include "Object.h"
#include "ScriptServer.h"
#include "ResourceManager.h"
#include "PhysicsServer.h"

class Game;

class Level {
friend class Game;
private:
	Game* owner = nullptr;
	std::vector<GameObject*> objects;
	std::vector<GameObject*> pendingObjects;
	bool shuttingDown = false;


public:
	Level() = default;
	// Полное описание ассета; сериализация не запускает жизненный цикл.
	rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const;
	bool Save(const std::string& path, std::string& error) const;
	// Новый уровень принадлежит вызывающему коду до передачи Game.
	static Level* Deserialize(const rapidjson::Value& json, const ComponentFactoryMap& factories, std::string& error);
	// Уровень принимает владение; возвращаемый указатель заимствованный.
	GameObject* LoadObjectFromFile(const std::string& path, std::string& error);
	~Level();
	Level(const Level&) = delete;
	Level& operator=(const Level&) = delete;
	Game* getOwner() const { return owner; }

	// При успехе уровень принимает владение объектом, созданным через new.
	bool addObject(GameObject* obj);
	GameObject* createObject();
	void destroyObject(GameObject* obj);
	// Уровень владеет объектами; редактор не меняет список напрямую.
	const std::vector<GameObject*>& getObjects() const { return objects; }
private:
	// Только владелец запускает жизненный цикл уровня.
	void Update(double deltaTime);
	void applyPendingChanges();
};

// Владеет уровнем и системами, которые обслуживают его компоненты.
class Game {
public:
	explicit Game(std::wstring windowTitle, long clientWidth = 1280, long clientHeight = 720);
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

	ScriptServer& getScriptServer() noexcept { return *scriptServer; }
	const ScriptServer& getScriptServer() const noexcept { return *scriptServer; }

	ResourceManager& getResourceManager() noexcept { return *resourceManager; }
	const ResourceManager& getResourceManager() const noexcept { return *resourceManager; }

	PhysicsServer& getPhysicsServer() noexcept { return physicsServer; }
	const PhysicsServer& getPhysicsServer() const noexcept { return physicsServer; }
	// Оба приложения выполняют физику после BeginFrame и до Update.
	bool StepPhysics();

	TimeManager& getTimeManager() noexcept { return timeManager; }
	const TimeManager& getTimeManager() const noexcept { return timeManager; }

	// При успехе принимает владение уровнем; при отказе владелец не меняется.
	bool setStartLevel(Level* level);
	// Загрузка стартового уровня разрешена только до Init.
	bool LoadLevelFromFile(const std::string& path, std::string& error);
	ComponentFactoryMap& getComponentFactoryMap() { return componentFactories; }
	const ComponentFactoryMap& getComponentFactoryMap() const { return componentFactories; }
	Level* getLevel() { return initialized ? currentLevel : nullptr; }

	void Init();
	int Run();

	// Общие этапы кадра для приложения игры и редактора.
	void BeginFrame();
	void Update();
	void EndFrame();

private:

	// Фабрики регистрируются при конструировании таблицы, отдельно для каждой Game.
	ComponentFactoryMap componentFactories;
	Level* currentLevel = nullptr;
	bool initialized = false;

	// Its address is stored in HWND; keep it alive through window destruction.
	WindowMessageContext windowContext{};
	TimeManager timeManager;
	PhysicsServer physicsServer;

	// Владеющие указатели: освобождаются после уровня в core.cpp.
	DisplayWindow32* window = nullptr;
	InputDevice* inputDevice = nullptr;
	RenderServer* renderServer = nullptr;
	ResourceManager* resourceManager = nullptr;
	// Состояние Lua живёт до освобождения всех компонентов уровня.
	ScriptServer* scriptServer = nullptr;




};
