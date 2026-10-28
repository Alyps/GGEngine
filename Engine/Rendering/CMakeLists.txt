# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(Engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(Engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MeshComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LightComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorCamera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CameraMovementComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderPresets.cpp"
)
