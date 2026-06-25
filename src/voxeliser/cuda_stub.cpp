#include "voxeliser/cuda.hpp"
#include "voxeliser/voxeliser.hpp"

#ifdef MULTIBLOCK_ENABLE_NANOVDB

#include <iostream>

namespace voxeliser {

bool UploadNanoVdbToGpu(const NanoVdbGrids& grids, GpuVoxelResources* out) {
	(void)grids;
	if (!out) {
		return false;
	}
	std::cerr << "CUDA upload stub: GPU support not enabled in this build.\n";
	return false;
}

std::vector<SurfaceVoxel> ExtractSurfaceVoxelsGpu(const NanoVdbGrids& grids, float isoValue) {
	(void)grids;
	(void)isoValue;
	std::cerr << "CUDA surface extraction stub: GPU support not enabled in this build.\n";
	return {};
}

} // namespace voxeliser

#endif
