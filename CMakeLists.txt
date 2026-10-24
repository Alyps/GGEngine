cmake_minimum_required (VERSION 3.13)


# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


# PhysX задаёт CRT через MSVC_RUNTIME_LIBRARY; политика включается до project.
if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
project ("GGEngine")


include(FetchContent)

# PhysX собираем из исходников; его настройки не затрагивают остальные зависимости.
function(add_physics)
	if(POLICY CMP0135)
		cmake_policy(SET CMP0135 NEW)
	endif()
	FetchContent_Declare(physx
		URL https://github.com/NVIDIA-Omniverse/PhysX/archive/refs/tags/106.4-physx-5.5.0.tar.gz
		SOURCE_SUBDIR nonexistent)
	# Сначала получаем исходники, чтобы передать SDK абсолютный корень.
	FetchContent_MakeAvailable(physx)
	set(PHYSX_ROOT_DIR "${physx_SOURCE_DIR}/physx")
	list(APPEND CMAKE_MODULE_PATH "${PHYSX_ROOT_DIR}/source/compiler/cmake/modules")
	set(TARGET_BUILD_PLATFORM windows)
	set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "" FORCE)
	# Используем тот же динамический CRT, что и движок, включая Debug.
	set(NV_USE_STATIC_WINCRT OFF CACHE BOOL "" FORCE)
	set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "" FORCE)
	set(PX_GENERATE_GPU_PROJECTS OFF)
	set(PX_COPY_EXTERNAL_DLL OFF CACHE BOOL "" FORCE)
	set(PUBLIC_RELEASE OFF)
	set(PX_OUTPUT_LIB_DIR "${physx_BINARY_DIR}")
	set(PX_OUTPUT_BIN_DIR "${physx_BINARY_DIR}")
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
	# Подключаем только SDK, без оболочки с примерами и копированием GPU DLL.
	add_subdirectory("${PHYSX_ROOT_DIR}/source/compiler/cmake" "${physx_BINARY_DIR}" EXCLUDE_FROM_ALL)
endfunction()
add_physics()

# Настройки статической Assimp ограничены областью функции и не меняют другие библиотеки.
function(add_mesh_importer)
	set(BUILD_SHARED_LIBS OFF)
	set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
	set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
	set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
	set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(assimp
		GIT_REPOSITORY https://github.com/assimp/assimp.git
		GIT_TAG v6.0.5
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(assimp)
endfunction()
add_mesh_importer()

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

# Lua берём только исходниками: у репозитория нет собственного CMake.
FetchContent_Declare(
	Lua
	GIT_REPOSITORY https://github.com/lua/lua.git
	GIT_TAG v5.4.8
	SOURCE_SUBDIR nonexistent
)
FetchContent_MakeAvailable(Lua)

# sol2 состоит из заголовков, поэтому его CMake не запускаем.
FetchContent_Declare(
	sol2
	GIT_REPOSITORY https://github.com/ThePhD/sol2.git
	GIT_TAG v3.5.0
	SOURCE_SUBDIR include
)
FetchContent_MakeAvailable(sol2)

# Явный список: без интерпретатора lua.c и однофайловой сборки onelua.c.
set(LUA_SOURCES
	${lua_SOURCE_DIR}/lapi.c ${lua_SOURCE_DIR}/lauxlib.c ${lua_SOURCE_DIR}/lbaselib.c
	${lua_SOURCE_DIR}/lcode.c ${lua_SOURCE_DIR}/lcorolib.c ${lua_SOURCE_DIR}/lctype.c
	${lua_SOURCE_DIR}/ldblib.c ${lua_SOURCE_DIR}/ldebug.c ${lua_SOURCE_DIR}/ldo.c
	${lua_SOURCE_DIR}/ldump.c ${lua_SOURCE_DIR}/lfunc.c ${lua_SOURCE_DIR}/lgc.c
	${lua_SOURCE_DIR}/linit.c ${lua_SOURCE_DIR}/liolib.c ${lua_SOURCE_DIR}/llex.c
	${lua_SOURCE_DIR}/lmathlib.c ${lua_SOURCE_DIR}/lmem.c ${lua_SOURCE_DIR}/loadlib.c
	${lua_SOURCE_DIR}/lobject.c ${lua_SOURCE_DIR}/lopcodes.c ${lua_SOURCE_DIR}/loslib.c
	${lua_SOURCE_DIR}/lparser.c ${lua_SOURCE_DIR}/lstate.c ${lua_SOURCE_DIR}/lstring.c
	${lua_SOURCE_DIR}/lstrlib.c ${lua_SOURCE_DIR}/ltable.c ${lua_SOURCE_DIR}/ltablib.c
	${lua_SOURCE_DIR}/ltm.c ${lua_SOURCE_DIR}/lundump.c ${lua_SOURCE_DIR}/lutf8lib.c
	${lua_SOURCE_DIR}/lvm.c ${lua_SOURCE_DIR}/lzio.c
)
# Сборка как C++: ошибки Lua идут через throw/catch внутри lua_pcall, а не longjmp.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${lua_SOURCE_DIR})




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
