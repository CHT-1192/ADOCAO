add_library(adocao_render STATIC
    Camera.cpp
    Shader.cpp
    TileGeometry.cpp
    TileMesh.cpp
    Planet.cpp
    PlanetTrail.cpp
)

target_include_directories(adocao_render PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(adocao_render
    PUBLIC adocao_core
    PRIVATE adocao_glad
)
