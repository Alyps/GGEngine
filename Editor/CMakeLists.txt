
add_executable(Editor "main.cpp")
target_link_libraries(Editor PRIVATE Engine)
target_compile_features(Editor PRIVATE cxx_std_20)
target_compile_definitions(Engine PUBLIC UNICODE _UNICODE)