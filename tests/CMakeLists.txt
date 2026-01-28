include(GoogleTest)

# Build separate test executables so we can label them for CI filtering.
add_executable(test_io_utils test_io_utils.cpp)
target_link_libraries(test_io_utils
    PRIVATE
        multiblock_lib
        VTK::IOGeometry
)

add_executable(test_vtk_vdb_converter test_vtk_vdb_converter.cpp)
target_link_libraries(test_vtk_vdb_converter
    PRIVATE
        multiblock_lib
        VTK::IOGeometry
)

# Link to gtest_main in a portable way: prefer the namespaced target from find_package,
# otherwise use the target provided by FetchContent (gtest_main).
foreach(tgt IN LISTS TEST_TARGETS)
    # placeholder to keep style consistent
endforeach()

if(TARGET GTest::gtest_main)
    target_link_libraries(test_io_utils PRIVATE GTest::gtest_main)
    target_link_libraries(test_vtk_vdb_converter PRIVATE GTest::gtest_main)
elseif(TARGET gtest_main)
    target_link_libraries(test_io_utils PRIVATE gtest_main)
    target_link_libraries(test_vtk_vdb_converter PRIVATE gtest_main)
else()
    message(FATAL_ERROR "gtest_main target not found. Ensure GTest is available or FetchContent succeeded.")
endif()

# Discover tests and add labels so CI can include/exclude them
# Use PROPERTIES here since discovered test names differ from target names.
gtest_discover_tests(test_io_utils
    PROPERTIES LABELS "light"
)

gtest_discover_tests(test_vtk_vdb_converter
    PROPERTIES LABELS "heavy;openvdb;vmtk"
)
