#include "fastvessels/io_utils.hpp"
#include "fastvessels/obj_pipeline.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
	std::cout << "Usage: " << argv0 << " -in <input.vtp> [-out <output.vtm>]" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
	std::string inputPath;
	std::string outputPath = "output/geometry_multiblock.vtm";

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
	return 0;
}
