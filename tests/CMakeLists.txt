foreach(_t block_test pcmfile_test timing_test)
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE y8960core)
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()

# エミュレータのライブラリが実行ファイルの隣に要るので、揃っているときだけ登録する。
add_executable(chips_test chips_test.cpp)
target_link_libraries(chips_test PRIVATE y8960core)
if(Y8960_EMULATOR_DIR)
    add_test(NAME chips_test COMMAND chips_test)
endif()
