#pragma once
#include <string>
#include <vector>
#include "Delegates.h"

class Game;
class ScriptConsole;

// Путь к скрипту в UTF-8, как его ввели в консоли. Вызывать Broadcast может только ScriptConsole.
DECLARE_EVENT(ScriptRequestedEvent, ScriptConsole, const std::string&);

// Консоль запуска скриптов внутри окна игры и редактора.
// Открывается клавишей ~ (тильда, VK_OEM_3), а закрывается кнопкой закрытия самого окна.
// Повторное нажатие ~ консоль не закрывает.
// Консоль не знает о Lua: она только собирает путь и сообщает о нём событием.
// Обработчик находится в Game (Game::OnScriptRequested) и вызывает ScriptServer::RunFile.
//
//   ввод ──► ScriptConsole ──(ScriptRequested)──► Game::OnScriptRequested ──► ScriptServer::RunFile
//
// TODO: файл только описывает структуру, реализации (ScriptConsole.cpp) пока нет.
//       Порядок работ, когда появится ImGui:
//       1. Добавить в Keys (InputDevice.h) клавишу Tilde = VK_OEM_3.
//       2. Реализовать методы ниже в ScriptConsole.cpp и добавить его в Engine/source/CMakeLists.txt.
//       3. Game владеет консолью (сырой указатель, как остальные системы), создаёт её в
//          конструкторе после ScriptServer и удаляет до него; подписывает OnScriptRequested.
//       4. Game::Run вызывает scriptConsole->Update() каждый кадр, а Draw() - в кадре ImGui.
class ScriptConsole {
public:
	// Подписка открыта всем; запускает событие только Submit().
	ScriptRequestedEvent ScriptRequested;

	// Следит за клавишей ~ и открывает консоль по нажатию (не по удержанию).
	// Если консоль уже открыта, нажатие ничего не меняет.
	// TODO: читать Keys::Tilde через InputDevice, запоминать прошлое состояние клавиши,
	//       как SpawnObjectsComponent в Editor/main.cpp. Символ '~' в строку не вставлять.
	void Update(Game& game);

	// Рисует окно консоли: заголовок с кнопкой закрытия, поле ввода, журнал вывода и историю.
	// TODO: реализовать через ImGui после его подключения. ImGui::Begin получает &open,
	//       поэтому кнопка закрытия сама сбрасывает флаг. Для поля ввода использовать
	//       ImGui::InputText с флагом EnterReturnsTrue; по Enter вызывать Submit().
	//       Ничего не рисовать при !isOpen().
	void Draw();

	bool isOpen() const { return open; }
	// Open ставит фокус на поле ввода при следующей отрисовке.
	void Open();
	// Close нужен коду движка; пользователь закрывает окно кнопкой.
	void Close();

	// Принимает введённую строку как путь к скрипту.
	// Пробелы по краям обрезаются, пустая строка игнорируется; иначе строка уходит в
	// историю, очищается, и вызывается ScriptRequested.Broadcast(path).
	// TODO: реализовать; порядок: обрезать, проверить на пустоту, сохранить в историю, Broadcast.
	void Submit();

	// Добавляет строку в журнал консоли (результат запуска, ошибка Lua, сообщение движка).
	// TODO: подключить к ScriptServer: print и ошибки RunFile должны попадать сюда,
	//       а не только в std::cout. Нужен выходной приёмник вместо прямого вывода в cout.
	void AddLogLine(const std::string& text);

	// Навигация по истории введённых путей стрелками вверх и вниз.
	// TODO: реализовать вместе с Draw(); историю ограничить по размеру.
	void HistoryPrevious();
	void HistoryNext();

	// TODO: пока консоль открыта, игровой ввод не должен срабатывать (WASD, мышь у
	//       CameraMovementComponent). Нужен флаг захвата ввода: Game или InputDevice
	//       спрашивают isOpen() (или ImGui WantCaptureKeyboard) перед обработкой клавиш.

private:
	bool open = false;
	// Строка ввода в UTF-8: ImGui::InputText работает с char-буфером.
	std::string line;
	// Журнал вывода и история введённых путей; размер ограничить при реализации.
	std::vector<std::string> logLines;
	std::vector<std::string> history;
	// Позиция в истории; равна history.size(), когда выбрана новая (пустая) строка.
	size_t historyIndex = 0;
	// Фокус на поле ввода нужно поставить один раз после открытия окна.
	bool focusInput = false;
	// Состояние ~ в прошлом кадре для срабатывания по нажатию.
	bool tildeWasDown = false;
};
