#pragma once

#ifdef MULTIBLOCK_ENABLE_VDB
#include <openvdb/openvdb.h>

namespace voxeliser {

using SdfGrid = openvdb::FloatGrid;
using LabelGrid = openvdb::Int32Grid;

} // namespace voxeliser

#endif
