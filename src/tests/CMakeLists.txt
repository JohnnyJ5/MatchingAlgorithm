foreach(algo gale_shapley hopcroft_karp hungarian blossom)
    add_executable(test_${algo} test_${algo}.cpp)
    target_link_libraries(test_${algo} PRIVATE algorithms GTest::gtest_main)
endforeach()
