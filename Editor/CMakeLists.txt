
add_executable(Editor "main.cpp" "Editor.cpp" "Editor.h")
target_sources(Editor PRIVATE "PhysicsTestScene.cpp")
target_link_libraries(Editor PRIVATE Engine EditorUI)
target_compile_features(Editor PRIVATE cxx_std_20)
target_compile_definitions(Engine PUBLIC UNICODE _UNICODE)
