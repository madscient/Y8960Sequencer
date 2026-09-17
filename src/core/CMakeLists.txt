add_library(y8960core STATIC
    block.cpp
    chips.cpp
    fmengine.cpp
    pcmfile.cpp
    platform.cpp
    player.cpp
    timing.cpp
)
target_include_directories(y8960core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT WIN32)
    target_link_libraries(y8960core PUBLIC ${CMAKE_DL_LIBS})
endif()
