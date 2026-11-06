# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(${engine_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MeshComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LightComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorCamera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderPresets.cpp"
)

# Навигация камеры является служебным компонентом редактора.
if(build_definition STREQUAL "GG_EDITOR")
    target_sources(${engine_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/CameraMovementComponent.cpp")
endif()
