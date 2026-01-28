#include <gtest/gtest.h>
#include "fastvessels/io_utils.hpp"
#include <vtkPolyData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkDataSet.h>
#include <filesystem>

// ------------------------------------------------------------------
// Test: CanReadSimpleSTL
// ------------------------------------------------------------------
TEST(IOUtils, CanReadSimpleSTL)
{
    // Read using the unified function
    auto dataset = fastvessels::ReadDataSet("../../tests/data/cube_1x1x1.stl");
    ASSERT_TRUE(dataset != nullptr);

    // Should be PolyData for STL files
    auto poly = vtkPolyData::SafeDownCast(dataset);
    ASSERT_TRUE(poly != nullptr);

    // Basic geometry sanity checks
    EXPECT_GT(poly->GetNumberOfPoints(), 0);
    EXPECT_GT(poly->GetNumberOfPolys(), 0);
}

// ------------------------------------------------------------------
// Optional: CanReadUnstructuredVTU
// ------------------------------------------------------------------
TEST(IOUtils, CanReadUnstructuredVTU)
{
    // This test will run only if you have a sample VTU file
    const std::string path = "../../tests/data/heart_sample.vtu";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Skipping: sample VTU not present at " << path;
    }
    auto dataset = fastvessels::ReadDataSet(path);
    ASSERT_TRUE(dataset != nullptr);

    auto grid = vtkUnstructuredGrid::SafeDownCast(dataset);
    ASSERT_TRUE(grid != nullptr);

    EXPECT_GT(grid->GetNumberOfPoints(), 0);
    EXPECT_GT(grid->GetNumberOfCells(), 0);
}

// ------------------------------------------------------------------
// Optional: CanHandleUppercaseExtensions
// ------------------------------------------------------------------
TEST(IOUtils, HandlesUppercaseExtensions)
{
    const std::string path = "../../tests/data/cube_1x1x1.STL"; // uppercase extension
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Skipping: filesystem is likely case-sensitive or file missing.";
    }
    auto dataset = fastvessels::ReadDataSet(path);
    ASSERT_TRUE(dataset != nullptr);

    auto poly = vtkPolyData::SafeDownCast(dataset);
    ASSERT_TRUE(poly != nullptr);
}
