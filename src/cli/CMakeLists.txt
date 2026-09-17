add_executable(y8960player main.cpp)
target_link_libraries(y8960player PRIVATE y8960core SDL3::SDL3-static)
