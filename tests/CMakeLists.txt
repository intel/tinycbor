add_executable(tst_c90 c90/tst_c90.c)
target_link_libraries(tst_c90 tinycbor)
add_test(NAME c90 COMMAND tst_c90)
