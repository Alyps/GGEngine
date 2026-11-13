# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(${engine_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ScriptServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ScriptBindingsMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ScriptBindingsTransform.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ScriptBindingsComponents.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UtilityAIComponent.cpp"
)

# Шаблоны sol2 в этом файле превышают лимит секций обычного объектного файла MSVC.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ScriptBindingsComponents.cpp" TARGET_DIRECTORY ${engine_target} PROPERTIES COMPILE_OPTIONS "/bigobj")
