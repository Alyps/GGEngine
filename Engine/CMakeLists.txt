
function(add_engine_variant engine_target build_definition)
    add_library(${engine_target} STATIC)
    # Определение общее для библиотеки и приложения, использующего её заголовки.
    target_compile_definitions(${engine_target} PUBLIC ${build_definition}=1 UNICODE _UNICODE)
    target_link_libraries(${engine_target} PUBLIC DirectXTK DirectXTex)
    target_link_libraries(${engine_target} PRIVATE Jolt)

    target_include_directories(${engine_target} SYSTEM PUBLIC "${rapidjson_SOURCE_DIR}/include")
    # Lua и sol2 используются только в .cpp движка и не попадают в публичные заголовки.
    target_link_libraries(${engine_target} PRIVATE lua)
    target_link_libraries(${engine_target} PRIVATE assimp::assimp)
    target_include_directories(${engine_target} SYSTEM PRIVATE "${sol2_SOURCE_DIR}/include")
    # Проверки типов включены во всех конфигурациях: скрипт с неверными аргументами не должен ронять движок.
    target_compile_definitions(${engine_target} PRIVATE SOL_NO_EXCEPTIONS=1 SOL_USING_CXX_LUA=1 SOL_ALL_SAFETIES_ON=1)
    target_compile_features(${engine_target} PUBLIC cxx_std_20)

    # У вариантов отдельные каталоги объектных файлов, но общие цели зависимостей.
    foreach(subsystem Core Level Transform Platform Rendering Physics Scripting Resources)
        add_subdirectory("${subsystem}" "${engine_target}/${subsystem}")
    endforeach()
endfunction()

add_engine_variant(EngineGame GG_GAME)
add_engine_variant(EngineEditor GG_EDITOR)
