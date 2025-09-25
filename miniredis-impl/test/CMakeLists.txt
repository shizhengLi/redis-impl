add_executable(test_runner test_sds.cpp test_dict.cpp)
target_link_libraries(test_runner miniredis)

add_test(NAME AllTests COMMAND test_runner)