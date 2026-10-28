
add_executable(Editor)
add_subdirectory("Application")
add_subdirectory("ContentBrowser")
target_link_libraries(Editor PRIVATE Engine EditorUI)
target_compile_features(Editor PRIVATE cxx_std_20)
target_compile_definitions(Engine PUBLIC UNICODE _UNICODE)

# Редактор использует стандартные ресурсы непосредственно из проекта Engine.
target_compile_definitions(Editor PRIVATE
    GG_STANDARD_RESOURCE_DIRECTORY="${CMAKE_SOURCE_DIR}/Engine/StandartResources"
)