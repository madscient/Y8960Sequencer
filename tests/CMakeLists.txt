foreach(_t block_test freqtab_test mute_test pcmfile_test sequencer_test timing_test wav_test)
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE y8960core)
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()

# コンソールと画面に出る文字列は ASCII に収める（doc/plan.md）。Python があるときだけ。
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME ascii_strings
             COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/check_ascii_strings.py
                     ${PROJECT_SOURCE_DIR}/src)
endif()

# エミュレータのライブラリが実行ファイルの隣に要るので、揃っているときだけ登録する。
foreach(_t chips_test render_test)
    add_executable(${_t} ${_t}.cpp)
    target_link_libraries(${_t} PRIVATE y8960core)
    if(Y8960_EMULATOR_DIR)
        add_test(NAME ${_t} COMMAND ${_t})
    endif()
endforeach()
