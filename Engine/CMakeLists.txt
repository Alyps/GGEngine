
add_library(Engine STATIC "start.cpp")
target_link_libraries(Engine PUBLIC DirectXTK DirectXTex)
target_link_libraries(Engine PRIVATE unofficial::omniverse-physx-sdk::sdk)
target_sources(Engine PRIVATE
    "source/Physics/PhysicsWorld.cpp"
    "source/Physics/PhysicsComponents.cpp"
)
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(Engine PUBLIC cxx_std_20)

add_subdirectory("source")
