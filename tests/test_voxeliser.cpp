#include "fastvessels/obj_pipeline.hpp"
#include "voxeliser/voxeliser.hpp"

#include <openvdb/openvdb.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <iostream>
#include <string>

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <input.vtp>" << std::endl;
		return 1;
	}

	const std::string inputPath = argv[1];

	auto blocks = fastvessels::ReadGeometry_AsMultiBlock(inputPath);
	if (!blocks || blocks->GetNumberOfBlocks() == 0) {
		std::cerr << "Failed to read input VTP." << std::endl;
		return 1;
	}

	auto refined = fastvessels::BuildRegionSurfaceHierarchy(blocks, "GroupId", "RegionId", true);
	if (!refined || refined->GetNumberOfBlocks() == 0) {
		std::cerr << "Failed to build region hierarchy." << std::endl;
		return 1;
	}

	fastvessels::AnalyzeRegionGroupSurfaces(refined, 1, false);

	voxeliser::VoxeliserConfig cfg;
	cfg.voxelSize = 1.0f;

	auto labeled = voxeliser::BuildLabeledSurfacePolyData(refined, cfg);
	if (!labeled || labeled->GetNumberOfCells() == 0) {
		std::cerr << "No labeled surface cells for voxelisation." << std::endl;
		return 1;
	}

	auto result = voxeliser::VoxelisePolyData(labeled, cfg);
	if (!result.sdf || result.sdf->activeVoxelCount() == 0) {
		std::cerr << "SDF grid is empty." << std::endl;
		return 1;
	}
	if (!result.region || result.region->activeVoxelCount() == 0) {
		std::cerr << "Region grid is empty." << std::endl;
		return 1;
	}
	if (!result.group || result.group->activeVoxelCount() == 0) {
		std::cerr << "Group grid is empty." << std::endl;
		return 1;
	}
	if (!result.surfaceType || result.surfaceType->activeVoxelCount() == 0) {
		std::cerr << "SurfaceType grid is empty." << std::endl;
		return 1;
	}
	if (result.surfaceVoxels.empty()) {
		std::cerr << "Surface voxel list is empty." << std::endl;
		return 1;
	}

	return 0;
}
