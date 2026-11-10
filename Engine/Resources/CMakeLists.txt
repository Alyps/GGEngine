# Заголовки подсистемы доступны по прежним именам include.
target_include_directories(${engine_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(${engine_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UtilityAIResource.cpp"
)
