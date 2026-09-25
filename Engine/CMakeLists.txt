
add_library(Engine STATIC "start.cpp")
target_link_libraries(Engine PRIVATE DirectXTK DirectXTex)