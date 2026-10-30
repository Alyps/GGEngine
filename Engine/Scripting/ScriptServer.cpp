#include "ScriptServer.h"
#include "ScriptBindings.h"
#include <iostream>
#include <new>
#include <string>
#include <sol/sol.hpp>
#include "ResourceManager.h"

struct ScriptServer::Impl {
	sol::state lua;
};

ScriptServer::~ScriptServer()
{
	delete impl;
}

bool ScriptServer::Init()
{
	if (impl) return true;
	impl = new (std::nothrow) Impl();
	if (!impl) return false;

	// Без io, os, package и debug: скрипты не получают доступ к системе.
	impl->lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
		sol::lib::table, sol::lib::utf8);

	// Типы движка регистрируем один раз: математика раньше типов, которые её используют.
	BindMath(impl->lua.lua_state());
	BindTransform(impl->lua.lua_state());
	BindComponents(impl->lua.lua_state());

	// print выводит в консоль движка с префиксом, а не в stdout процесса напрямую.
	impl->lua.set_function("print", [](sol::this_state state, sol::variadic_args args) {
		lua_State* L = state;
		std::string line;
		int index = 0;
		for (auto arg : args) {
			// luaL_tolstring учитывает __tostring и кладёт строку на стек.
			size_t length = 0;
			const char* text = luaL_tolstring(L, arg.stack_index(), &length);
			if (index++ > 0) line += '\t';
			line.append(text, length);
			lua_pop(L, 1);
		}
		std::cout << "[Lua] " << line << std::endl;
	});
	return true;
}

bool ScriptServer::RunFile(const std::string& path, std::string& error)
{
	error.clear();
	if (!impl) {
		error = path + ": script server is not initialized";
		return false;
	}

	// Ошибка загрузки или выполнения возвращается результатом и не прерывает движок.
	const std::string resolved = resources ? resources->ResolvePath(path) : path;
	if (resolved.empty()) { error = "Invalid script path"; return false; }
	sol::protected_function_result result = impl->lua.safe_script_file(resolved, sol::script_pass_on_error);
	if (!result.valid()) {
		sol::error failure = result;
		error = path + ": " + failure.what();
		return false;
	}
	return true;
}

lua_State* ScriptServer::getState() const
{
	return impl ? impl->lua.lua_state() : nullptr;
}
