add_executable(mtdd_flex_meta_test flex_meta_test.cpp)
target_link_libraries(mtdd_flex_meta_test PRIVATE GTest::gtest GTest::gtest_main flatbuffers::flatbuffers)
target_include_directories(mtdd_flex_meta_test PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_test(NAME flex_meta_test COMMAND mtdd_flex_meta_test)
