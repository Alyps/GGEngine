
add_executable (Game "main.cpp")
target_link_libraries (Game PRIVATE Engine)
target_compile_features (Game PRIVATE cxx_std_20)