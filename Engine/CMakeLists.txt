
add_library(Engine STATIC)
target_link_libraries(Engine PUBLIC DirectXTK DirectXTex)
target_link_libraries(Engine PRIVATE PhysXExtensions)

target_include_directories(Engine SYSTEM PRIVATE "${rapidjson_SOURCE_DIR}/include")
# Lua и sol2 используются только в .cpp движка и не попадают в публичные заголовки.
target_link_libraries(Engine PRIVATE lua)
target_link_libraries(Engine PRIVATE assimp::assimp)
target_include_directories(Engine SYSTEM PRIVATE "${sol2_SOURCE_DIR}/include")
# Проверки типов включены во всех конфигурациях: скрипт с неверными аргументами не должен ронять движок.
target_compile_definitions(Engine PRIVATE SOL_NO_EXCEPTIONS=1 SOL_USING_CXX_LUA=1 SOL_ALL_SAFETIES_ON=1)
target_compile_features(Engine PUBLIC cxx_std_20)

add_subdirectory("Core")
add_subdirectory("Level")
add_subdirectory("Transform")
add_subdirectory("Platform")
add_subdirectory("Rendering")
add_subdirectory("Physics")
add_subdirectory("Scripting")
add_subdirectory("Resources")
