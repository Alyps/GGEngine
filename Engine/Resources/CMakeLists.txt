# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(Engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(Engine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager.cpp"
)
