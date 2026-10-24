#pragma once

struct lua_State;

// Внутренние функции регистрации Lua-типов; вызываются из ScriptServer::Init.
// Порядок важен: Transform и компоненты используют типы из BindMath.
void BindMath(lua_State* L);
void BindTransform(lua_State* L);
void BindComponents(lua_State* L);
