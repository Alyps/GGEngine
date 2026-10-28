
add_executable(Editor "main.cpp" "Editor.cpp" "Editor.h" "ContentBrowser.cpp" "ContentBrowser.h")
target_link_libraries(Editor PRIVATE Engine EditorUI)
target_compile_features(Editor PRIVATE cxx_std_20)
target_compile_definitions(Engine PUBLIC UNICODE _UNICODE)

# Штатные ресурсы входят в папку поставки рядом с Editor.exe.
add_custom_command(TARGET Editor POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Editor>/res/StandartResources/Shaders"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/Engine/source/StandartResources/Default.material.json"
		"$<TARGET_FILE_DIR:Editor>/res/StandartResources/Default.material.json"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/Engine/source/StandartResources/Shaders/Simple.hlsl"
		"$<TARGET_FILE_DIR:Editor>/res/StandartResources/Shaders/Simple.hlsl"
	VERBATIM
)
