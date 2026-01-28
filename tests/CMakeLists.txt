include(GoogleTest)

# Single light test executable
add_executable(test_io_utils test_io_utils.cpp)
target_link_libraries(test_io_utils PRIVATE multiblock_lib VTK::IOGeometry)

if(TARGET GTest::gtest_main)
    target_link_libraries(test_io_utils PRIVATE GTest::gtest_main)
elseif(TARGET gtest_main)
    target_link_libraries(test_io_utils PRIVATE gtest_main)
else()
    message(FATAL_ERROR "gtest_main target not found. Ensure GTest is available or FetchContent succeeded.")
endif()

gtest_discover_tests(test_io_utils PROPERTIES LABELS "light")
