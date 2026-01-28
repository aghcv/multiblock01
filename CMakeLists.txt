cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(multiblock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Ensure CMake searches the conda environment's cmake path
if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}/lib/cmake")
endif()

# Dependencies: minimal VTK only
find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    IOXML
    IOLegacy
    IOGeometry
)

# Core library (used by tests and executable)
add_library(multiblock_lib STATIC
    src/io_utils.cpp
)
target_include_directories(multiblock_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multiblock_lib PUBLIC
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::IOXML
    VTK::IOLegacy
    VTK::IOGeometry
)

# Executable (minimal)
add_executable(multiblock src/main.cpp)
target_include_directories(multiblock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multiblock PRIVATE multiblock_lib
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML VTK::IOLegacy VTK::IOGeometry)

vtk_module_autoinit(TARGETS multiblock multiblock_lib MODULES
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML VTK::IOLegacy VTK::IOGeometry)

# Unit Tests
enable_testing()
include(FetchContent)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found; Fetching googletest via FetchContent...")
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(tests)
