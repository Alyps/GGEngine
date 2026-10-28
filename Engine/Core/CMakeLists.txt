# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(Engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(Engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/core.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Time.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Delegates.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp"
)

target_sources(Engine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/start.cpp"
)
