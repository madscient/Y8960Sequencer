add_library(y8960core STATIC
    block.cpp
    chips.cpp
    dev_dcsg.cpp
    dev_opl2ex.cpp
    dev_opllex.cpp
    dev_scc.cpp
    dev_ssgs.cpp
    device.cpp
    fmengine.cpp
    freqtab.cpp
    pcmfile.cpp
    pitch.cpp
    platform.cpp
    player.cpp
    sequencer.cpp
    timing.cpp
)
target_include_directories(y8960core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT WIN32)
    target_link_libraries(y8960core PUBLIC ${CMAKE_DL_LIBS})
endif()
