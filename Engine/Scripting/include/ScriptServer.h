#pragma once
#include <string>

struct lua_State;
class ResourceManager;

// Владеет единственным состоянием Lua. Заголовок не включает Lua и sol2.
class ScriptServer {
private:
	struct Impl;
	// Владеющий указатель: создаётся в Init, освобождается в деструкторе.
	Impl* impl = nullptr;
	// Заимствован у Game: ресурсы живут дольше состояния Lua.
	const ResourceManager* resources = nullptr;

public:
	explicit ScriptServer(const ResourceManager* manager = nullptr) : resources(manager) {}
	~ScriptServer();
	ScriptServer(const ScriptServer&) = delete;
	ScriptServer& operator=(const ScriptServer&) = delete;
	ScriptServer(ScriptServer&&) = delete;
	ScriptServer& operator=(ScriptServer&&) = delete;

	// Возвращает false, если состояние не удалось создать.
	bool Init();

	// Выполняет Lua-файл один раз в общем состоянии, без компонента и без self.
	// Путь UTF-8, относительно корня менеджера или рабочей папки без менеджера.
	// Возвращаемое значение скрипта отбрасывается;
	// глобальные переменные и функции остаются доступны другим скриптам.
	// При ошибке возвращает false и записывает причину в error; движок продолжает работу.
	bool RunFile(const std::string& path, std::string& error);

	// nullptr до успешного Init. Указатель действителен, пока жив сервер.
	lua_State* getState() const;
};
