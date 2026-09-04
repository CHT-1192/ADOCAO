add_library(adocao_glad STATIC gl_core.cpp)

target_include_directories(adocao_glad PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(adocao_glad PUBLIC glfw)
