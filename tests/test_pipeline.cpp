#include "fastvessels/io_utils.hpp"
#include "fastvessels/obj_pipeline.hpp"

#include <vtkMultiBlockDataSet.h>
#include <vtkSmartPointer.h>
#include <vtkXMLMultiBlockDataReader.h>

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
	if (argc < 3) {
		std::cerr << "Usage: " << argv[0] << " <input.vtp> <output.vtm>" << std::endl;
		return 1;
	}

	const std::string inputPath = argv[1];
	const std::string outputPath = argv[2];

	auto blocks = fastvessels::ReadGeometry_AsMultiBlock(inputPath);
	if (!blocks || blocks->GetNumberOfBlocks() == 0) {
		std::cerr << "Failed to read input VTP or no blocks produced." << std::endl;
		return 1;
	}

	auto refined = fastvessels::BuildRegionSurfaceHierarchy(blocks, "GroupId", "RegionId", true);
	if (!refined || refined->GetNumberOfBlocks() == 0) {
		std::cerr << "Failed to build region hierarchy." << std::endl;
		return 1;
	}

	fastvessels::AnalyzeRegionGroupSurfaces(refined, 1, false);

	const std::filesystem::path outPath(outputPath);
	if (!outPath.parent_path().empty()) {
		std::filesystem::create_directories(outPath.parent_path());
	}
	fastvessels::WriteMultiBlock(refined, outputPath);

	if (!std::filesystem::exists(outPath)) {
		std::cerr << "Output VTM was not written." << std::endl;
		return 1;
	}
	if (std::filesystem::file_size(outPath) == 0) {
		std::cerr << "Output VTM is empty." << std::endl;
		return 1;
	}

	auto reader = vtkSmartPointer<vtkXMLMultiBlockDataReader>::New();
	reader->SetFileName(outputPath.c_str());
	reader->Update();

	vtkMultiBlockDataSet* outMb = vtkMultiBlockDataSet::SafeDownCast(reader->GetOutput());
	if (!outMb || outMb->GetNumberOfBlocks() == 0) {
		std::cerr << "Output VTM contains no blocks." << std::endl;
		return 1;
	}

	return 0;
}
