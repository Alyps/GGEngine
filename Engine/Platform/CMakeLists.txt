# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(${engine_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Игровой ввод отсутствует в исходниках редакторской библиотеки.
if(build_definition STREQUAL "GG_GAME")
    target_sources(${engine_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/GameInput.cpp")
endif()

target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/InputDevice.cpp"
)
