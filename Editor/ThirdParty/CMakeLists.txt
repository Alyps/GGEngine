add_library(EditorUI STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "ImGuizmo.cpp")
target_include_directories(EditorUI PUBLIC
    "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imguizmo_SOURCE_DIR}")
target_compile_definitions(EditorUI PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_compile_features(EditorUI PUBLIC cxx_std_20)
target_link_libraries(EditorUI PRIVATE d3d11 d3dcompiler dwmapi)

