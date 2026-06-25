cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(multiblock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

option(ENABLE_VDB "Enable OpenVDB voxeliser" ON)
option(ENABLE_CUDA "Enable CUDA acceleration for NanoVDB" OFF)

if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}/lib/cmake")
endif()

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    IOXML
    FiltersCore
    FiltersGeometry
)

set(MULTIBLOCK_HAVE_VDB OFF)
set(MULTIBLOCK_HAVE_NANOVDB OFF)

if(ENABLE_VDB)
    find_package(OpenVDB QUIET)
    if(OpenVDB_FOUND)
        set(MULTIBLOCK_HAVE_VDB ON)
        add_compile_definitions(MULTIBLOCK_ENABLE_VDB)

        find_path(NANOVDB_INCLUDE_DIR nanovdb/NanoVDB.h)
        if(NANOVDB_INCLUDE_DIR)
            set(MULTIBLOCK_HAVE_NANOVDB ON)
            add_compile_definitions(MULTIBLOCK_ENABLE_NANOVDB)
        else()
            find_path(NANOVDB_INCLUDE_DIR openvdb/nanovdb/NanoVDB.h)
            if(NANOVDB_INCLUDE_DIR)
                set(MULTIBLOCK_HAVE_NANOVDB ON)
                add_compile_definitions(MULTIBLOCK_ENABLE_NANOVDB)
            endif()
        endif()
    else()
        message(WARNING "OpenVDB not found; voxeliser will be disabled.")
    endif()
endif()

if(ENABLE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        add_compile_definitions(MULTIBLOCK_ENABLE_CUDA)
        enable_language(CUDA)
    else()
        message(WARNING "CUDA toolkit not found; GPU voxeliser stubs will be used.")
    endif()
endif()

add_library(multiblock_lib STATIC
    src/geometry/mblock_util.cpp
    src/geometry/io_utils.cpp
)
target_include_directories(multiblock_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multiblock_lib PUBLIC
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::IOXML
    VTK::FiltersCore
    VTK::FiltersGeometry
)

set(VOXELISER_SOURCES
    src/voxeliser/voxeliser.cpp
    src/voxeliser/cuda_stub.cpp
)

if(MULTIBLOCK_HAVE_NANOVDB)
    list(APPEND VOXELISER_SOURCES src/voxeliser/nanovdb_utils.cpp)
endif()
if(ENABLE_CUDA AND CUDAToolkit_FOUND)
    list(APPEND VOXELISER_SOURCES src/voxeliser/cuda_kernels.cu)
endif()

add_library(voxeliser_lib STATIC ${VOXELISER_SOURCES})
target_include_directories(voxeliser_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(NANOVDB_INCLUDE_DIR)
    target_include_directories(voxeliser_lib PUBLIC ${NANOVDB_INCLUDE_DIR})
endif()
target_link_libraries(voxeliser_lib PUBLIC
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::IOXML
    VTK::FiltersCore
    VTK::FiltersGeometry
)
if(MULTIBLOCK_HAVE_VDB)
    target_link_libraries(voxeliser_lib PUBLIC OpenVDB::openvdb)
endif()
if(ENABLE_CUDA AND CUDAToolkit_FOUND)
    target_link_libraries(voxeliser_lib PUBLIC CUDA::cudart)
endif()

add_executable(multiblock src/app/app_main.cpp)
target_include_directories(multiblock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multiblock PRIVATE multiblock_lib
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML
    VTK::FiltersCore VTK::FiltersGeometry
    voxeliser_lib)

set_target_properties(multiblock PROPERTIES
    SUFFIX ".mbx"
)

vtk_module_autoinit(TARGETS multiblock multiblock_lib voxeliser_lib MODULES
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML
    VTK::FiltersCore VTK::FiltersGeometry)

enable_testing()

add_executable(multiblock_tests tests/test_pipeline.cpp)
target_include_directories(multiblock_tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(multiblock_tests PRIVATE multiblock_lib
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML
    VTK::FiltersCore VTK::FiltersGeometry
    voxeliser_lib)

vtk_module_autoinit(TARGETS multiblock_tests MODULES
    VTK::CommonCore VTK::CommonDataModel VTK::IOXML
    VTK::FiltersCore VTK::FiltersGeometry)

add_test(NAME vtp_to_vtm COMMAND multiblock_tests
    ${CMAKE_SOURCE_DIR}/raw/vtp/cardiovascular.vtp
    ${CMAKE_BINARY_DIR}/test_output.vtm)

if(MULTIBLOCK_HAVE_VDB)
    add_executable(voxeliser_tests tests/test_voxeliser.cpp)
    target_include_directories(voxeliser_tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(voxeliser_tests PRIVATE voxeliser_lib multiblock_lib
        VTK::CommonCore VTK::CommonDataModel VTK::IOXML
        VTK::FiltersCore VTK::FiltersGeometry)

    vtk_module_autoinit(TARGETS voxeliser_tests MODULES
        VTK::CommonCore VTK::CommonDataModel VTK::IOXML
        VTK::FiltersCore VTK::FiltersGeometry)

    add_test(NAME voxeliser_cpu COMMAND voxeliser_tests
        ${CMAKE_SOURCE_DIR}/raw/vtp/cardiovascular.vtp)
endif()
