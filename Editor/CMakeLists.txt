
add_executable(Editor)
add_subdirectory("Application")
add_subdirectory("ContentBrowser")
target_link_libraries(Editor PRIVATE EngineEditor EditorUI)
target_compile_features(Editor PRIVATE cxx_std_20)

# Кладём стандартные ресурсы рядом с EXE выбранной конфигурации.
add_custom_command(TARGET Editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/StandartResources"
        "$<TARGET_FILE_DIR:Editor>/StandartResources"
    VERBATIM
)
