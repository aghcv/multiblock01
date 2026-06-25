#include "voxeliser/nanovdb_utils.hpp"

#ifdef MULTIBLOCK_ENABLE_NANOVDB

#include <filesystem>

namespace voxeliser {

bool WriteNanoVdbGrids(const NanoVdbGrids& grids, const std::string& path) {
	if (path.empty()) {
		return false;
	}

	const bool hasAny = static_cast<bool>(grids.sdf) || static_cast<bool>(grids.region) ||
		static_cast<bool>(grids.group) || static_cast<bool>(grids.surfaceType);
	if (!hasAny) {
		return false;
	}

	std::filesystem::path base(path);
	const std::string stem = base.stem().string();
	const std::string ext = base.extension().empty() ? ".nvdb" : base.extension().string();
	const std::filesystem::path dir = base.parent_path();
	if (!dir.empty()) {
		std::filesystem::create_directories(dir);
	}

	if (grids.sdf) {
		nanovdb::io::writeGrid((dir / (stem + "_sdf" + ext)).string(), grids.sdf);
	}
	if (grids.region) {
		nanovdb::io::writeGrid((dir / (stem + "_region" + ext)).string(), grids.region);
	}
	if (grids.group) {
		nanovdb::io::writeGrid((dir / (stem + "_group" + ext)).string(), grids.group);
	}
	if (grids.surfaceType) {
		nanovdb::io::writeGrid((dir / (stem + "_surface" + ext)).string(), grids.surfaceType);
	}

	return true;
}

} // namespace voxeliser

#endif
