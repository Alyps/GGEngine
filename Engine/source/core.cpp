#include "core.h"

Game::Game(std::wstring windowTitle, long clientWidth, long clientHeight)
{
	DisplayWindow32::InitWindowClass();
	window = new DisplayWindow32(clientWidth, clientHeight, windowTitle.c_str(), windowContext);
	inputDevice = new InputDevice(*window);
	windowContext.inputDevice = inputDevice;
	renderServer = new RenderServer();
	renderServer->Init(*window);
	// Ошибка создания Lua не прерывает запуск: скриптовые компоненты сообщат о ней сами.
	scriptServer = new ScriptServer();
	scriptServer->Init();

}

Game::~Game() noexcept
{
	// Уровень освобождается первым: его LuaScriptComponent используют состояние Lua.
	delete start_level;
	delete scriptServer;
	delete renderServer;
	delete inputDevice;
	delete window;
}

void Game::Init()
{
	current_level = start_level;
	InitCurrentLevel();
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

		timeManager.update();

		const auto& fixedTime = timeManager.get_fixedTime();

		// Catch-up: 0, 1 или несколько фиксированных шагов.
		for (std::uint64_t step = 0;
			step < fixedTime.get_steps();
			++step)
		{
			// Будущая физика:
			// physics.Update(fixedTime.get_interval());
		}

		// Обновляем трансформации до получения матриц камеры рендером.
		if (current_level)
			current_level->Update(*this, timeManager.get_deltaTime());

		// Для будущей интерполяции при отрисовке:
		// const double alpha = fixedTime.get_alpha();

		renderServer->DrawAll();
		if (current_level) current_level->applyPendingChanges(*this);
	}
}


Level::~Level()
{
    shuttingDown = true;
    for (GameObject* object : objects) delete object;
    for (GameObject* object : pendingObjects) delete object;
}

void Level::Init(Game& game)
{
    applyPendingChanges(game);
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

void Level::applyPendingChanges(Game& game)
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
            object->applyPendingChanges(game);
            ++it;
        }
    }
    objects.insert(objects.end(), additions.begin(), additions.end());
    for (GameObject* object : additions) {
        if (object->pendingDestroy) {
            objects.erase(std::find(objects.begin(), objects.end(), object));
            delete object;
        } else {
            object->Init(game);
        }
    }
}

void Level::Update(Game& game, double deltaTime)
{
    for (GameObject* object : objects)
        if (!object->pendingDestroy) object->Update(game, deltaTime);
}
