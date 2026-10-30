#include "core.h"

Game::Game(std::wstring windowTitle, long clientWidth, long clientHeight)
{
	DisplayWindow32::InitWindowClass();
	window = new DisplayWindow32(clientWidth, clientHeight, windowTitle.c_str(), windowContext);
	inputDevice = new InputDevice(*window);
	windowContext.inputDevice = inputDevice;
	renderServer = new RenderServer();
	renderServer->Init(*window);
	resourceManager = new ResourceManager();
	// Ошибка создания Lua не прерывает запуск: скриптовые компоненты сообщат о ней сами.
	scriptServer = new ScriptServer();
	scriptServer->Init();

}

Game::~Game() noexcept
{
	// Уровень освобождается первым: его LuaScriptComponent используют состояние Lua.
	delete currentLevel;
	physicsServer.Shutdown();
	delete scriptServer;
	delete resourceManager;
	delete renderServer;
	// Оконные сообщения больше не должны обращаться к удаляемому вводу.
	windowContext.inputDevice = nullptr;
	delete inputDevice;
	delete window;
}

bool Game::setStartLevel(Level* level)
{
	if (initialized) return false;
	if (currentLevel == level) return true;
	if (level && level->owner) return false;
	delete currentLevel;
	currentLevel = level;
	if (currentLevel) currentLevel->owner = this;
	return true;
}

void Game::Init()
{
	if (initialized) return;
	initialized = true;
	if (currentLevel) currentLevel->applyPendingChanges();
}

bool Game::replaceLevel(Level* level, const std::string& projectDirectory)
{
	if (!level || level->owner) return false;
	// Готовим новые системы до удаления сцены: отказ оставляет прежний уровень живым.
	auto* nextResources = new (std::nothrow) ResourceManager(projectDirectory);
	if (!nextResources) return false;
	nextResources->SetStandardResourceDirectory(resourceManager->GetStandardResourceDirectory());
	auto* nextScripts = new (std::nothrow) ScriptServer(nextResources);
	if (!nextScripts || !nextScripts->Init()) {
		delete nextScripts;
		delete nextResources;
		return false;
	}
	// Shutdown компонентов использует старые серверы и кеш до их освобождения.
	delete currentLevel;
	currentLevel = nullptr;
	physicsServer.Shutdown();
	delete scriptServer;
	delete resourceManager;
	scriptServer = nextScripts;
	resourceManager = nextResources;
	currentLevel = level;
	currentLevel->owner = this;
	initialized = true;
	currentLevel->applyPendingChanges();
	timeManager.reset();
	return true;
}

void Game::BeginFrame()
{
	timeManager.update();
}

void Game::Update()
{
	if (initialized && currentLevel)
		currentLevel->Update(timeManager.get_deltaTime());
}

void Game::EndFrame()
{
	// Удаляем компоненты только после завершения обходов и отрисовки.
	if (initialized && currentLevel) currentLevel->applyPendingChanges();
}

int Game::Run()
{

	// Не включаем время загрузки уровня в первый кадр.
	timeManager.reset();

	for (;;)
	{
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
				return static_cast<int>(message.wParam);

			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		BeginFrame();
		if (!StepPhysics()) return 1;
		Update();
		renderServer->DrawAll();
		EndFrame();
	}
}


bool Game::StepPhysics()
{
	if (!physicsServer.getLastError().empty()) return false;
	const auto& fixed = timeManager.get_fixedTime();
	// TimeManager задаёт число шагов, отдельный аккумулятор не нужен.
	for (std::uint64_t step = 0; step < fixed.get_steps(); ++step)
		if (!physicsServer.Update(fixed.get_interval())) return false;
	return true;
}

Level::~Level()
{
    shuttingDown = true;
    for (GameObject* object : objects) delete object;
    for (GameObject* object : pendingObjects) delete object;
}

bool Level::addObject(GameObject* obj)
{
    if (!obj || obj->owner || shuttingDown) return false;
    obj->owner = this;
    pendingObjects.push_back(obj);
    return true;
}

GameObject* Level::createObject()
{
    if (shuttingDown) return nullptr;
    auto* object = new (std::nothrow) GameObject();
    if (object) addObject(object);
    return object;
}

bool Level::addObjectAt(GameObject* object, size_t position)
{
    if (!addObject(object)) return false;
    pendingObjectPositions.emplace(object, position);
    return true;
}

void Level::destroyObject(GameObject* obj)
{
    if (obj && obj->owner == this) obj->pendingDestroy = true;
}

void Level::applyPendingChanges()
{
    // Запросы, поступившие во время очистки и Init, ждут следующего кадра.
    std::vector<GameObject*> additions;
    additions.swap(pendingObjects);
    for (auto it = objects.begin(); it != objects.end();) {
        GameObject* object = *it;
        if (object->pendingDestroy) {
            it = objects.erase(it);
            delete object;
        } else {
            object->applyPendingChanges();
            ++it;
        }
    }
    for (GameObject* object : additions) {
        const auto position = pendingObjectPositions.find(object);
        if (position == pendingObjectPositions.end()) objects.push_back(object);
        else {
            objects.insert(objects.begin() + (std::min)(position->second, objects.size()), object);
            pendingObjectPositions.erase(position);
        }
    }
    for (GameObject* object : additions) {
        if (object->pendingDestroy) {
            objects.erase(std::find(objects.begin(), objects.end(), object));
            delete object;
        } else {
            object->applyPendingChanges();
        }
    }
}

void Level::Update(double deltaTime)
{
    for (GameObject* object : objects)
        if (!object->pendingDestroy) object->Update(deltaTime);
}

rapidjson::Value Level::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    error.clear();
    if (shuttingDown) { error = "Cannot serialize a level during shutdown"; return {}; }
    rapidjson::Value json(rapidjson::kObjectType), serializedObjects(rapidjson::kArrayType);
    json.AddMember("asset_type", "Level", allocator);
    json.AddMember("version", 1, allocator);
    // В будущем объект может храниться как путь к ассету; сейчас импорт раскрывается полностью.
    // Сохраняем и ещё не инициализированные объекты, не меняя очереди уровня.
    for (const auto* list : { &objects, &pendingObjects }) {
        for (const auto* object : *list) {
            if (object->isPendingDestroy()) continue;
            auto entry = object->Serialize(allocator, error);
            if (entry.IsNull()) {
                error = "Object " + std::to_string(serializedObjects.Size()) + ": " + error;
                return {};
            }
            serializedObjects.PushBack(entry, allocator);
        }
    }
    json.AddMember("objects", serializedObjects, allocator);
    return json;
}

bool Level::Save(const std::string& path, std::string& error) const
{
    rapidjson::Document document;
    auto json = Serialize(document.GetAllocator(), error);
    if (json.IsNull()) return false;
    return Serialization::SaveFile(json, path, error);
}

Level* Level::Deserialize(const rapidjson::Value& json, const ComponentFactoryMap& factories, std::string& error)
{
    if (!Deserialization::Asset(json, "Level", error)) return nullptr;
    const auto* entries = Deserialization::Array(json, "objects", error);
    if (!entries) return nullptr;
    auto* level = new (std::nothrow) Level();
    if (!level) { error = "Cannot allocate Level"; return nullptr; }
    // В будущем здесь появится загрузка по пути; пока все объекты полностью вложены.
    for (rapidjson::SizeType i = 0; i < entries->Size(); ++i) {
        GameObject* object = GameObject::Deserialize((*entries)[i], factories, error);
        if (!object) {
            error = "objects[" + std::to_string(i) + "]: " + error;
            delete level; return nullptr;
        }
        if (!level->addObject(object)) {
            delete object;
            delete level;
            error = "Cannot attach deserialized object to level";
            return nullptr;
        }
    }
    return level;
}

bool Game::LoadLevelFromFile(const std::string& path, std::string& error)
{
    error.clear();
    if (initialized) { error = "Start level can only be loaded before Game::Init"; return false; }
    rapidjson::Document json;
    if (!Deserialization::LoadFile(path, json, error)) return false;
    Level* level = Level::Deserialize(json, componentFactories, error);
    if (!level) { error = path + ": " + error; return false; }
    // Старый стартовый уровень заменяется только после полного восстановления нового.
    if (!setStartLevel(level)) {
        delete level;
        error = "Cannot attach loaded level to Game";
        return false;
    }
    return true;
}

GameObject* Level::LoadObjectFromFile(const std::string& path, std::string& error)
{
    error.clear();
    if (!owner || shuttingDown) { error = "Level must belong to a Game and not be shutting down"; return nullptr; }
    rapidjson::Document json;
    if (!Deserialization::LoadFile(path, json, error)) return nullptr;
    GameObject* object = GameObject::Deserialize(json, owner->getComponentFactoryMap(), error);
    if (!object) { error = path + ": " + error; return nullptr; }
    // Импорт создаёт независимую копию; дальнейшее сохранение уровня раскрывает её целиком.
    if (!addObject(object)) {
        delete object;
        error = "Cannot attach loaded object to level";
        return nullptr;
    }
    return object;
}
