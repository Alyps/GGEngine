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

		// Логика, обновляемая один раз за кадр:
		// current_level->Update(timeManager.get_deltaTime());

		// Для будущей интерполяции при отрисовке:
		// const double alpha = fixedTime.get_alpha();

		renderServer->DrawAll();
	}
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