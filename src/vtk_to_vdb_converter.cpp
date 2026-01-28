// Minimal implementation of VTK <-> OpenVDB converters
#include "fastvessels/vtk_to_vdb_converter.hpp"

#include <openvdb/openvdb.h>
#include <vtkImageData.h>

namespace fastvessels {

openvdb::FloatGrid::Ptr ConvertVTKToVDB(vtkSmartPointer<vtkImageData> image)
{
    static bool initialized = false;
    if (!initialized) { openvdb::initialize(); initialized = true; }

    // Minimal placeholder: create an empty float grid
    auto grid = openvdb::FloatGrid::create(/*background=*/0.0f);
    grid->setName("density");
    return grid;
}

vtkSmartPointer<vtkImageData> ConvertVDBToVTK(const openvdb::FloatGrid::Ptr& /*grid*/)
{
    // Minimal placeholder: return a tiny image
    auto img = vtkSmartPointer<vtkImageData>::New();
    img->SetDimensions(1, 1, 1);
    img->AllocateScalars(VTK_FLOAT, 1);
    return img;
}

void ConvertDataSet(const std::string& /*inputPath*/, const std::string& /*outputPath*/)
{
    // Intentionally left minimal; full conversion requires IO plumbing.
}

} // namespace fastvessels
