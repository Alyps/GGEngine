cmake_minimum_required (VERSION 3.13)


# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("GGEngine")


include(FetchContent)

FetchContent_Declare(
	DirectXTK
	GIT_REPOSITORY https://github.com/microsoft/DirectXTK.git
	GIT_TAG may2026
)
FetchContent_MakeAvailable(DirectXTK)


FetchContent_Declare(
	DirectXTex
	GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
	GIT_TAG may2026
)
FetchContent_MakeAvailable(DirectXTex)

# Используем только заголовки JSON, без примеров и тестов библиотеки.
FetchContent_Declare(
	RapidJSON
	GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
	GIT_TAG v1.1.0
	SOURCE_SUBDIR include
)
FetchContent_MakeAvailable(RapidJSON)




# Зависимости интерфейса используются только приложением редактора.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5-docking)
FetchContent_MakeAvailable(imgui)
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 1.83)
FetchContent_MakeAvailable(imguizmo)

add_library(EditorUI STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "Editor/ImGuizmo.cpp")
target_include_directories(EditorUI PUBLIC
    "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imguizmo_SOURCE_DIR}")
target_compile_definitions(EditorUI PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_compile_features(EditorUI PUBLIC cxx_std_20)
target_link_libraries(EditorUI PRIVATE d3d11 d3dcompiler dwmapi)

add_subdirectory ("Engine") # Static Library
add_subdirectory ("Editor") # executable
add_subdirectory ("Game")	# executable
