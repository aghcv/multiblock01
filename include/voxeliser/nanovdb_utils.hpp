#pragma once

#include <string>

#ifdef MULTIBLOCK_ENABLE_NANOVDB
#if __has_include(<nanovdb/NanoVDB.h>)
#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/CreateNanoGrid.h>
#include <nanovdb/util/IO.h>
#elif __has_include(<openvdb/nanovdb/NanoVDB.h>)
#include <openvdb/nanovdb/NanoVDB.h>
#include <openvdb/nanovdb/util/CreateNanoGrid.h>
#include <openvdb/nanovdb/util/IO.h>
#endif

namespace voxeliser {

struct NanoVdbGrids {
	nanovdb::GridHandle<nanovdb::HostBuffer> sdf;
	nanovdb::GridHandle<nanovdb::HostBuffer> region;
	nanovdb::GridHandle<nanovdb::HostBuffer> group;
	nanovdb::GridHandle<nanovdb::HostBuffer> surfaceType;
};

bool WriteNanoVdbGrids(const NanoVdbGrids& grids, const std::string& path);

} // namespace voxeliser

#endif
