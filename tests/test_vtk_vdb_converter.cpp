#include <gtest/gtest.h>
#include <vtkImageData.h>
#include "fastvessels/vtk_to_vdb_converter.hpp"

// Create a tiny image (2x2x2) for conversion tests
static vtkSmartPointer<vtkImageData> makeTinyImage() {
    auto img = vtkSmartPointer<vtkImageData>::New();
    img->SetDimensions(2, 2, 2);
    img->AllocateScalars(VTK_FLOAT, 1);
    return img;
}

TEST(VTKVDBConverter, RoundTripStructured)
{
    auto img = makeTinyImage();
    auto grid = fastvessels::ConvertVTKToVDB(img);
    ASSERT_TRUE(grid != nullptr);

    auto back = fastvessels::ConvertVDBToVTK(grid);
    ASSERT_TRUE(back != nullptr);
}
