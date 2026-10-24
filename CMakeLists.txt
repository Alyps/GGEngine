cmake_minimum_required (VERSION 3.13)


# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("GGEngine")


include(FetchContent)

find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

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




add_subdirectory ("Engine") # Static Library
add_subdirectory ("Editor") # executable
add_subdirectory ("Game")	# executable
