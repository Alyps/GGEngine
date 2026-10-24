
add_library(Engine STATIC "start.cpp")
target_link_libraries(Engine PUBLIC DirectXTK DirectXTex)
target_link_libraries(Engine PRIVATE unofficial::omniverse-physx-sdk::sdk)
target_sources(Engine PRIVATE
    "source/PhysicsServer.cpp"
    "source/StandartResources/PhysicsComponents.cpp"
)
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(Engine SYSTEM PRIVATE "${rapidjson_SOURCE_DIR}/include")
# Lua и sol2 используются только в .cpp движка и не попадают в публичные заголовки.
target_link_libraries(Engine PRIVATE lua)
target_include_directories(Engine SYSTEM PRIVATE "${sol2_SOURCE_DIR}/include")
# Проверки типов включены во всех конфигурациях: скрипт с неверными аргументами не должен ронять движок.
target_compile_definitions(Engine PRIVATE SOL_NO_EXCEPTIONS=1 SOL_USING_CXX_LUA=1 SOL_ALL_SAFETIES_ON=1)
target_compile_features(Engine PUBLIC cxx_std_20)

add_subdirectory("source")
