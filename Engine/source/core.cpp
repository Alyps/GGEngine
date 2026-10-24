#include "core.h"

Game::Game(std::wstring windowTitle, long clientWidth, long clientHeight)
{
	DisplayWindow32::InitWindowClass();
	window = new DisplayWindow32(clientWidth, clientHeight, windowTitle.c_str(), windowContext);
	inputDevice = new InputDevice(*window);
	windowContext.inputDevice = inputDevice;
	renderServer = new RenderServer();
	renderServer->Init(*window);

}

Game::~Game() noexcept
{
	physicsWorld.Shutdown();
	while (renderComponents) renderComponents->Detach();
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
	if (initializationFailed) return 1;

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
			if (!physicsWorld.Step(fixedTime.get_interval())) return 1;
		}

		// Логика, обновляемая один раз за кадр:
		// current_level->Update(timeManager.get_deltaTime());

		// Для будущей интерполяции при отрисовке:
		// const double alpha = fixedTime.get_alpha();

		for (RenderComponent* component = renderComponents; component; component = component->nextRender)
			component->SyncTransform();
		renderServer->DrawAll();
	}
}

void Game::registerRenderComponent(RenderComponent& component)
{
	component.registeredGame = this;
	component.previousRender = nullptr;
	component.nextRender = renderComponents;
	if (renderComponents) renderComponents->previousRender = &component;
	renderComponents = &component;
}

void Game::unregisterRenderComponent(RenderComponent& component)
{
	if (component.registeredGame != this) return;
	if (component.previousRender) component.previousRender->nextRender = component.nextRender;
	else renderComponents = component.nextRender;
	if (component.nextRender) component.nextRender->previousRender = component.previousRender;
	component.registeredGame = nullptr;
	component.nextRender = component.previousRender = nullptr;
}


void Level::Init(Game& game)
{
	for (GameObject* obj : objects)
	{
		obj->Init(game);
	}
}

void Level::addObject(GameObject* obj)
{
	objects.push_back(obj);
}