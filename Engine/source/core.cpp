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
		Update();
		renderServer->DrawAll();
		EndFrame();
	}
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
    objects.insert(objects.end(), additions.begin(), additions.end());
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
