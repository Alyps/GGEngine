target_sources(Editor PRIVATE main.cpp Editor.cpp Editor.h Project.cpp Project.h EditorSettings.cpp EditorSettings.h)
target_include_directories(Editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# JSON уже подключён движком; редактор использует ту же зависимость для манифеста.
target_include_directories(Editor SYSTEM PRIVATE "${rapidjson_SOURCE_DIR}/include")
