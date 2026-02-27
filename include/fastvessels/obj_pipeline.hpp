#pragma once

#include <string>

#include <vtkMultiBlockDataSet.h>
#include <vtkSmartPointer.h>

namespace fastvessels {

vtkSmartPointer<vtkMultiBlockDataSet> ReadGeometry_AsMultiBlock(const std::string& path);

void AnalyzeRegionGroupSurfaces(vtkMultiBlockDataSet* regions, int maxRegionThreads, bool unifyWalls);

vtkSmartPointer<vtkMultiBlockDataSet> BuildRegionSurfaceHierarchy(
	vtkMultiBlockDataSet* inputMb,
	const std::string& groupArrayName = "GroupId",
	const std::string& regionArrayName = "RegionId",
	bool addOriginalBlockId = true);

} // namespace fastvessels
