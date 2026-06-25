#include "fastvessels/io_utils.hpp"
#include "fastvessels/obj_pipeline.hpp"
#include "voxeliser/voxeliser.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
	std::cout << "Usage: " << argv0
			  << " -in <input.vtp> [-out <output.vtm>]"
			  << " [--voxelise --voxel-size <float> --export-vdb <path> --export-nanovdb <path>]"
			  << std::endl;
}

} // namespace

int main(int argc, char** argv) {
	std::string inputPath;
	std::string outputPath = "output/geometry_multiblock.vtm";
	bool voxelise = false;
	float voxelSize = 0.5f;
	std::string exportVdbPath;
	std::string exportNanoVdbPath;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if ((arg == "-h") || (arg == "--help")) {
			PrintUsage(argv[0]);
			return 0;
		}
		if (arg == "-in" && i + 1 < argc) {
			inputPath = argv[++i];
			continue;
		}
		if (arg == "-out" && i + 1 < argc) {
			outputPath = argv[++i];
			continue;
		}
		if (arg == "--voxelise") {
			voxelise = true;
			continue;
		}
		if (arg == "--voxel-size" && i + 1 < argc) {
			voxelSize = std::stof(argv[++i]);
			continue;
		}
		if (arg == "--export-vdb" && i + 1 < argc) {
			exportVdbPath = argv[++i];
			continue;
		}
		if (arg == "--export-nanovdb" && i + 1 < argc) {
			exportNanoVdbPath = argv[++i];
			continue;
		}
		if (inputPath.empty() && !arg.empty() && arg[0] != '-') {
			inputPath = arg;
			continue;
		}
		std::cerr << "Unknown argument: " << arg << std::endl;
		PrintUsage(argv[0]);
		return 1;
	}

	if (inputPath.empty()) {
		PrintUsage(argv[0]);
		return 1;
	}

	auto blocks = fastvessels::ReadGeometry_AsMultiBlock(inputPath);
	auto refined = fastvessels::BuildRegionSurfaceHierarchy(blocks, "GroupId", "RegionId", true);
	fastvessels::AnalyzeRegionGroupSurfaces(refined, 1, false);

	const std::filesystem::path outPath(outputPath);
	if (!outPath.parent_path().empty()) {
		std::filesystem::create_directories(outPath.parent_path());
	}
	fastvessels::WriteMultiBlock(refined, outputPath);

	std::cout << "Wrote multiblock: " << outputPath << std::endl;

	if (voxelise) {
#ifdef MULTIBLOCK_ENABLE_VDB
		voxeliser::VoxeliserConfig cfg;
		cfg.voxelSize = voxelSize;

		auto labeled = voxeliser::BuildLabeledSurfacePolyData(refined, cfg);
		if (!labeled || labeled->GetNumberOfCells() == 0) {
			std::cerr << "Voxelisation failed: no labeled surface cells found." << std::endl;
			return 1;
		}

		auto result = voxeliser::VoxelisePolyData(labeled, cfg);
		if (!exportVdbPath.empty()) {
			const std::filesystem::path vdbPath(exportVdbPath);
			if (!vdbPath.parent_path().empty()) {
				std::filesystem::create_directories(vdbPath.parent_path());
			}
			if (!voxeliser::ExportVdbGrids(result, exportVdbPath)) {
				std::cerr << "Failed to export VDB grids: " << exportVdbPath << std::endl;
			}
		}

#ifdef MULTIBLOCK_ENABLE_NANOVDB
		if (!exportNanoVdbPath.empty()) {
			const std::filesystem::path nanoPath(exportNanoVdbPath);
			if (!nanoPath.parent_path().empty()) {
				std::filesystem::create_directories(nanoPath.parent_path());
			}
			auto nano = voxeliser::ConvertToNanoVdb(result);
			if (!voxeliser::ExportNanoVdbGrids(nano, exportNanoVdbPath)) {
				std::cerr << "Failed to export NanoVDB grids: " << exportNanoVdbPath << std::endl;
			}
		}
#else
		if (!exportNanoVdbPath.empty()) {
			std::cerr << "NanoVDB export requested but NanoVDB is not enabled." << std::endl;
		}
#endif

		std::cout << "Voxelisation complete: surface voxels=" << result.surfaceVoxels.size() << std::endl;
#else
		std::cerr << "Voxelisation requested but OpenVDB support is disabled." << std::endl;
		return 1;
#endif
	}
	return 0;
}
