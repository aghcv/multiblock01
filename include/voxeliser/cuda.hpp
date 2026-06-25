#pragma once

#include <vector>

namespace voxeliser {

struct SurfaceVoxel;

#ifdef MULTIBLOCK_ENABLE_NANOVDB
struct NanoVdbGrids;
#endif

struct GpuVoxelResources {
	void* sdf = nullptr;
	void* region = nullptr;
	void* group = nullptr;
	void* surfaceType = nullptr;
};

#ifdef MULTIBLOCK_ENABLE_NANOVDB
bool UploadNanoVdbToGpu(const NanoVdbGrids& grids, GpuVoxelResources* out);
std::vector<SurfaceVoxel> ExtractSurfaceVoxelsGpu(const NanoVdbGrids& grids, float isoValue);
#endif

} // namespace voxeliser
