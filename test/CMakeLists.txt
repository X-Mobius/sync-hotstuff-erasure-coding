include_directories(../src/
                    ../salticidae/include/
                    ../)

add_executable(test_secp256k1 test_secp256k1.cpp)
target_link_libraries(test_secp256k1 hotstuff_static)

add_executable(test_functions
    ./test_function.c   # 指定测试文件的路径
    ../src/code_function.c   # 添加路径
)

target_link_libraries(test_functions hotstuff_static)

add_executable(test_reproposal_rs
    ./test_reproposal_rs.c
    ../src/code_function.c
)

target_link_libraries(test_reproposal_rs hotstuff_static)

add_executable(test_command_batch test_command_batch.cpp ../src/code_function.c)
target_link_libraries(test_command_batch hotstuff_static)

add_executable(test_full_proposal test_full_proposal.cpp)
target_link_libraries(test_full_proposal hotstuff_static)

add_executable(test_full_command_state test_full_command_state.cpp ../src/code_function.c)
target_link_libraries(test_full_command_state hotstuff_static)
