# Backend библиотеки собирается один раз и использует то же устройство DX11.
if(NOT TARGET GGUIBackend)
    FetchContent_GetProperties(rmlui SOURCE_DIR gg_rmlui_source)
    add_library(GGUIBackend STATIC "${gg_rmlui_source}/Backends/RmlUi_Renderer_DX11.cpp")
    target_include_directories(GGUIBackend PUBLIC "${gg_rmlui_source}/Backends")
    target_link_libraries(GGUIBackend PUBLIC RmlUi::Core PRIVATE d3d11 d3dcompiler)
    target_compile_features(GGUIBackend PRIVATE cxx_std_20)
endif()

target_include_directories(${engine_target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${engine_target} PRIVATE GGUIBackend)
target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/UIDocumentComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UIServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UIRenderer.cpp")
