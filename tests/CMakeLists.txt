find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(seek_tests
    extract_test.cpp
    index_test.cpp
    search_test.cpp
    snippet_test.cpp
    sync_test.cpp
    tokenizer_test.cpp)
target_link_libraries(seek_tests PRIVATE seek_core seek_warnings GTest::gtest_main)

gtest_discover_tests(seek_tests)
