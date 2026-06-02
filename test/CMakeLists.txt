add_executable(mtdd_flex_meta_test
  flex_meta_test.cpp
  ${CMAKE_SOURCE_DIR}/src/codec/flex_meta.cpp
)
target_link_libraries(mtdd_flex_meta_test PRIVATE GTest::gtest GTest::gtest_main flatbuffers::flatbuffers)
target_include_directories(mtdd_flex_meta_test PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_test(NAME flex_meta_test COMMAND mtdd_flex_meta_test)

add_executable(mtdd_notify_registry_test
  notify_registry_test.cpp
  ${CMAKE_SOURCE_DIR}/src/notify/channel_key.cpp
  ${CMAKE_SOURCE_DIR}/src/notify/registry.cpp
)
target_link_libraries(mtdd_notify_registry_test PRIVATE GTest::gtest GTest::gtest_main mtdd_proto)
target_include_directories(mtdd_notify_registry_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
)

add_test(NAME notify_registry_test COMMAND mtdd_notify_registry_test)
