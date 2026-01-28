cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(multiblock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}/lib/cmake")
endif()

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    IOXML
    IOLegacy
    IOGeometry
)

add_library(multiblock_lib STATIC
    src/common.cpp
    src/env_info.cpp
    src/config.cpp
    src/solver.cpp
    src/preprocess.cpp
    src/postprocess.cpp
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

add_executable(multiblock src/app_main.cpp)
target_include_directories(multiblock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multiblock PRIVATE multiblock_lib
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML VTK::IOLegacy VTK::IOGeometry)

set_target_properties(multiblock PROPERTIES
    SUFFIX ".mbx"
)

vtk_module_autoinit(TARGETS multiblock multiblock_lib MODULES
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML VTK::IOLegacy VTK::IOGeometry)

# No unit tests on the 'light' branch
