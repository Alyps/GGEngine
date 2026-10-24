
add_executable(Editor "main.cpp" "Editor.cpp" "Editor.h" "ContentBrowser.cpp" "ContentBrowser.h")
target_link_libraries(Editor PRIVATE Engine EditorUI)
target_compile_features(Editor PRIVATE cxx_std_20)
target_compile_definitions(Engine PUBLIC UNICODE _UNICODE)
