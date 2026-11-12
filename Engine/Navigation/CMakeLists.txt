# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(${engine_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Компонент объёма и NavigationServer (Detour) нужны обоим вариантам движка.
target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/NavigationServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/NavMeshVolumeComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/NavAgentComponent.cpp"
)

# Генерация (Recast) и отладочная отрисовка существуют только в редакторе.
if(build_definition STREQUAL "GG_EDITOR")
    target_sources(${engine_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/NavMeshBuilder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/NavMeshDebugDraw.cpp"
    )
endif()
