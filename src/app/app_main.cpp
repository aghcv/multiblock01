#include "fastvessels/io_utils.hpp"
#include "fastvessels/obj_pipeline.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
	std::cout << "Usage: " << argv0 << " <input.vtp> [output.vtm]" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		PrintUsage(argv[0]);
		return 1;
	}

	const std::string inputPath = argv[1];
	const std::string outputPath = (argc >= 3) ? argv[2] : "output/geometry_multiblock.vtm";

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
