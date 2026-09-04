add_library(adocao_audio STATIC
    AudioEngine.cpp
    HitsoundManager.cpp
    stb_vorbis_impl.cpp
)

target_include_directories(adocao_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(adocao_audio PUBLIC adocao_core)
target_include_directories(adocao_audio PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)
