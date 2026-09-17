add_library(y8960app STATIC engine.cpp)
target_include_directories(y8960app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(y8960app PUBLIC y8960core SDL3::SDL3-static)
