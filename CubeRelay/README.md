# Cube Relay

За 45 секунд соберите три золотых кубика и доберитесь до зелёного выхода. У голубого кубика три жизни. Красный дрон и активная оранжевая полоса отнимают жизнь при касании.

Нажмите **пробел**, чтобы начать. **WASD** или **стрелки** двигают игрока. **R** запускает раунд заново.

В верхней части экрана видны запас времени, жизни и собранные кубики. Внизу отображаются действие дрона и состояние полосы.

## Сборка на Windows

Нужны Visual Studio 2022 с компонентами C++ для классических приложений, Windows SDK, CMake, Ninja и Git. Скачайте ветку проекта.

~~~powershell
git clone --branch QA https://github.com/Alyps/GGEngine.git
cd GGEngine
~~~

Если репозиторий уже скачан, выполните в его корне `git switch QA` и `git pull --ff-only`.

Из корня репозитория в **Developer PowerShell for VS 2022** с выбранной архитектурой x64 выполните команды сборки.

~~~powershell
cmake --preset x64-debug -B out/build/x64-qa-debug '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES='
cmake --build out/build/x64-qa-debug --target Game Editor
~~~

## Запуск

- **Без редактора.** Откройте двойным щелчком [Play.vbs](Play.vbs). Игра загружает основную сцену и сохраняет сообщения в `CubeRelay/game.log`.
- **В редакторе.** Откройте `out/build/x64-qa-debug/Editor/Editor.exe`, выберите **File → Open Project...**, укажите [CubeRelay.ggproject](CubeRelay.ggproject) и нажмите **Play**.

Для запуска в PowerShell из корня репозитория подойдёт та же сборка Game.exe.

~~~powershell
& .\out\build\x64-qa-debug\Game\Game.exe --project (Resolve-Path .\CubeRelay).Path --scene 'res://Scenes/Main.level.json' --resources res --standard-resources (Resolve-Path .\Engine\StandartResources).Path
~~~

Замените `Main.level.json` на `Autoplay.level.json` в этой команде, чтобы посмотреть автоматическое прохождение.

## Как устроена игра

- `res/Scenes/Main.level.json` содержит арену, камеру, кубики и интерфейс. Проект открывается в редакторе через `CubeRelay.ggproject`.
- `res/Scripts/Arena.lua` отвечает за ввод, урон, сбор, победу и перезапуск. Стандартный `TimerComponent` завершает раунд через 45 секунд.
- `res/Scripts/Pulse.lua` подписывается на повторяющийся таймер. Каждые две секунды полоса меняет состояние.
- `res/AI/drone.utilityai.json` настраивает `UtilityAIComponent`. Дрон выбирает патруль, погоню или зарядку по расстоянию до игрока и запасу энергии.
- `res/Scenes/Autoplay.level.json` использует те же правила раунда и ведёт игрока по контрольным точкам автоматически.
