#pragma once

#include <string>
#include <vector>

#include <vtkSmartPointer.h>

#include "voxeliser/vdb_types.hpp"
#ifdef MULTIBLOCK_ENABLE_NANOVDB
#include "voxeliser/nanovdb_utils.hpp"
#endif

class vtkMultiBlockDataSet;
class vtkPolyData;

namespace voxeliser {

struct VoxeliserConfig {
	float voxelSize = 0.5f;
	float exteriorBandwidth = 3.0f;
	float interiorBandwidth = 3.0f;
	float surfaceBandVoxels = 1.5f;
	std::string regionArrayName = "RegionId";
	std::string groupArrayName = "GroupId";
	std::string surfaceTypeArrayName = "SurfaceType";
};

struct SurfaceVoxel {
	int x = 0;
	int y = 0;
	int z = 0;
};

struct VoxelisedResult {
#ifdef MULTIBLOCK_ENABLE_VDB
	openvdb::FloatGrid::Ptr sdf;
	openvdb::Int32Grid::Ptr region;
	openvdb::Int32Grid::Ptr group;
	openvdb::Int32Grid::Ptr surfaceType;
#endif
	std::vector<SurfaceVoxel> surfaceVoxels;
};

vtkSmartPointer<vtkPolyData> BuildLabeledSurfacePolyData(
	vtkMultiBlockDataSet* regions,
	const VoxeliserConfig& config);

VoxelisedResult VoxelisePolyData(vtkPolyData* poly, const VoxeliserConfig& config);

bool ExportVdbGrids(const VoxelisedResult& result, const std::string& path);

#ifdef MULTIBLOCK_ENABLE_NANOVDB
NanoVdbGrids ConvertToNanoVdb(const VoxelisedResult& result);
bool ExportNanoVdbGrids(const NanoVdbGrids& grids, const std::string& path);
#endif

} // namespace voxeliser
