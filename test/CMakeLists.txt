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

