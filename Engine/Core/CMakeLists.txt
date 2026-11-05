# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(${engine_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/core.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Time.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Delegates.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp"
)

target_sources(${engine_target} PRIVATE)
