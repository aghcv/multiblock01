#include "fastvessels/obj_pipeline.hpp"

#include <vtkAbstractArray.h>
#include <vtkAppendPolyData.h>
#include <vtkCellData.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkConnectivityFilter.h>
#include <vtkDataArray.h>
#include <vtkExtractCells.h>
#include <vtkFieldData.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkIntArray.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPointDataToCellData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTriangleFilter.h>
#include <vtkXMLPolyDataReader.h>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace fastvessels {

static std::string JoinArrayNames(vtkDataSetAttributes* attrs);
static std::string JoinFieldArrayNames(vtkFieldData* attrs);
static void LogDataArrays(const std::string& label, vtkPolyData* pd);
static bool PromoteGroupArrayFromFieldData(
	vtkPolyData* pd,
	const std::vector<std::string>& candidates,
	std::string& resolvedName);
static void SetBlockName(vtkMultiBlockDataSet* mb, unsigned int idx, const std::string& name);

vtkSmartPointer<vtkMultiBlockDataSet> ReadGeometry_AsMultiBlock(const std::string& path) {
	namespace fs = std::filesystem;

	if (!fs::exists(path)) {
		throw std::runtime_error("Geometry file not found: " + path);
	}

	std::string ext = fs::path(path).extension().string();
	for (auto& c : ext) c = static_cast<char>(std::tolower(c));

	if (ext != ".vtp") {
		throw std::runtime_error("Unsupported geometry format (only .vtp): " + ext);
	}

	auto reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
	reader->SetFileName(path.c_str());
	reader->Update();

	vtkPolyData* pd = reader->GetOutput();
	if (!pd || pd->GetNumberOfPoints() == 0) {
		throw std::runtime_error("Failed to read VTP: " + path);
	}

	LogDataArrays("VTP read", pd);
	std::string resolvedName;
	std::vector<std::string> candidates = {
		"GroupId",
		"GroupIds",
		"groupIds",
		"group_id",
		"groupId",
		"group_ids",
		"GroupID",
		"groupID"
	};
	if (PromoteGroupArrayFromFieldData(pd, candidates, resolvedName)) {
		std::cerr << "Info: promoted Group array '" << resolvedName
				  << "' from FieldData to DataSet attributes.\n";
		LogDataArrays("VTP after field promotion", pd);
	}

	auto blocks = vtkSmartPointer<vtkMultiBlockDataSet>::New();
	blocks->SetNumberOfBlocks(1);
	blocks->SetBlock(0, pd);
	blocks->GetMetaData(0u)->Set(vtkCompositeDataSet::NAME(), "geometry");
	return blocks;
}

static std::string GetBlockName(vtkMultiBlockDataSet* mb, unsigned int idx) {
	if (!mb) return "";
	vtkInformation* info = mb->GetMetaData(idx);
	if (!info) return "";
	const char* name = info->Get(vtkCompositeDataSet::NAME());
	return name ? std::string(name) : "";
}

static double MeanCellNormalAngle(vtkPolyData* pd) {
	if (!pd || pd->GetNumberOfCells() == 0) {
		return 3.141592653589793;
	}

	auto normalsFilter = vtkSmartPointer<vtkPolyDataNormals>::New();
	normalsFilter->SetInputData(pd);
	normalsFilter->ComputeCellNormalsOn();
	normalsFilter->ComputePointNormalsOff();
	normalsFilter->SplittingOff();
	normalsFilter->ConsistencyOn();
	normalsFilter->AutoOrientNormalsOn();
	normalsFilter->Update();

	vtkPolyData* out = normalsFilter->GetOutput();
	if (!out) {
		return 3.141592653589793;
	}

	vtkDataArray* normals = out->GetCellData()->GetNormals();
	if (!normals || normals->GetNumberOfTuples() == 0) {
		return 3.141592653589793;
	}

	double sum[3] = {0.0, 0.0, 0.0};
	const vtkIdType n = normals->GetNumberOfTuples();
	for (vtkIdType i = 0; i < n; ++i) {
		double v[3] = {0.0, 0.0, 0.0};
		normals->GetTuple(i, v);
		sum[0] += v[0];
		sum[1] += v[1];
		sum[2] += v[2];
	}
	const double len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
	if (len <= 1e-8) {
		return 3.141592653589793;
	}
	sum[0] /= len;
	sum[1] /= len;
	sum[2] /= len;

	double angleSum = 0.0;
	for (vtkIdType i = 0; i < n; ++i) {
		double v[3] = {0.0, 0.0, 0.0};
		normals->GetTuple(i, v);
		double dot = v[0] * sum[0] + v[1] * sum[1] + v[2] * sum[2];
		dot = std::max(-1.0, std::min(1.0, dot));
		angleSum += std::acos(dot);
	}
	return angleSum / static_cast<double>(n);
}

static void SetSurfaceTypeField(vtkPolyData* pd, bool isXlet) {
	if (!pd) return;
	vtkFieldData* field = pd->GetFieldData();
	if (!field) return;
	field->RemoveArray("SurfaceType");

	auto arr = vtkSmartPointer<vtkIntArray>::New();
	arr->SetName("SurfaceType");
	arr->SetNumberOfComponents(1);
	arr->SetNumberOfTuples(1);
	arr->SetValue(0, isXlet ? 1 : 0);
	field->AddArray(arr);
}


static void SetRegionCountField(vtkPolyData* pd, const char* name, int value) {
	if (!pd) return;
	vtkFieldData* field = pd->GetFieldData();
	if (!field) return;
	field->RemoveArray(name);

	auto arr = vtkSmartPointer<vtkIntArray>::New();
	arr->SetName(name);
	arr->SetNumberOfComponents(1);
	arr->SetNumberOfTuples(1);
	arr->SetValue(0, value);
	field->AddArray(arr);
}

static int GetSurfaceTypeField(vtkPolyData* pd) {
	if (!pd) return -1;
	vtkFieldData* field = pd->GetFieldData();
	if (!field) return -1;
	auto* arr = vtkIntArray::SafeDownCast(field->GetAbstractArray("SurfaceType"));
	if (!arr || arr->GetNumberOfTuples() < 1) return -1;
	return arr->GetValue(0);
}


void AnalyzeRegionGroupSurfaces(vtkMultiBlockDataSet* regions, int maxRegionThreads, bool unifyWalls) {
	if (!regions) {
		return;
	}

	const int regionCount = static_cast<int>(regions->GetNumberOfBlocks());
	if (regionCount <= 0) {
		return;
	}
	(void)maxRegionThreads;
	const double flatAngleRad = 0.20;

	int totalGroups = 0;
	int totalXlets = 0;
	int totalWalls = 0;

	for (int r = 0; r < regionCount; ++r) {
		auto regionMb = vtkMultiBlockDataSet::SafeDownCast(regions->GetBlock(static_cast<unsigned int>(r)));
		if (!regionMb) continue;
		const int groupCount = static_cast<int>(regionMb->GetNumberOfBlocks());
		if (groupCount <= 0) continue;

		int xletTotal = 0;
		int wallTotal = 0;
		for (int g = 0; g < groupCount; ++g) {
			vtkPolyData* pd = vtkPolyData::SafeDownCast(
				regionMb->GetBlock(static_cast<unsigned int>(g)));
			if (!pd) continue;
			const double meanAngle = MeanCellNormalAngle(pd);
			const bool isXlet = meanAngle <= flatAngleRad;
			SetSurfaceTypeField(pd, isXlet);
			if (isXlet) {
				++xletTotal;
			} else {
				++wallTotal;
			}
		}

		totalGroups += groupCount;
		totalXlets += xletTotal;
		totalWalls += wallTotal;

		const std::string regionName = GetBlockName(regions, static_cast<unsigned int>(r));
		std::cout << "Region " << (regionName.empty() ? std::to_string(r) : regionName)
				  << ": groups=" << groupCount
				  << " xlets=" << xletTotal
				  << " walls=" << (unifyWalls ? (wallTotal > 0 ? 1 : 0) : wallTotal)
				  << std::endl;
	}

	int unifiedWallRegions = 0;
	if (unifyWalls) {
		for (int r = 0; r < regionCount; ++r) {
			auto regionMb = vtkMultiBlockDataSet::SafeDownCast(regions->GetBlock(static_cast<unsigned int>(r)));
			if (!regionMb) continue;
			const int groupCount = static_cast<int>(regionMb->GetNumberOfBlocks());
			if (groupCount <= 0) continue;

			std::vector<vtkSmartPointer<vtkPolyData>> xletBlocks;
			std::vector<std::string> xletNames;
			auto wallAppender = vtkSmartPointer<vtkAppendPolyData>::New();
			int wallCount = 0;
			int xletCount = 0;

			for (int g = 0; g < groupCount; ++g) {
				auto* pd = vtkPolyData::SafeDownCast(regionMb->GetBlock(static_cast<unsigned int>(g)));
				if (!pd) continue;
				const int surfaceType = GetSurfaceTypeField(pd);
				if (surfaceType == 1) {
					xletBlocks.emplace_back(pd);
					xletNames.emplace_back(GetBlockName(regionMb, static_cast<unsigned int>(g)));
					++xletCount;
				} else {
					wallAppender->AddInputData(pd);
					++wallCount;
				}
			}

			unsigned int outCount = static_cast<unsigned int>(xletBlocks.size() + (wallCount > 0 ? 1 : 0));
			auto regionOut = vtkSmartPointer<vtkMultiBlockDataSet>::New();
			regionOut->SetNumberOfBlocks(outCount);

			unsigned int outIdx = 0;
			for (size_t i = 0; i < xletBlocks.size(); ++i) {
				auto xletCopy = vtkSmartPointer<vtkPolyData>::New();
				xletCopy->ShallowCopy(xletBlocks[i]);
				SetRegionCountField(xletCopy, "RegionXletCount", xletCount);
				SetRegionCountField(xletCopy, "RegionWallGroupCount", wallCount);
				regionOut->SetBlock(outIdx, xletCopy);
				std::string name = xletNames[i];
				if (name.empty()) {
					name = "Xlet_" + std::to_string(outIdx);
				}
				SetBlockName(regionOut, outIdx, name);
				++outIdx;
			}

			if (wallCount > 0) {
				wallAppender->Update();
				auto wallCombined = vtkSmartPointer<vtkPolyData>::New();
				wallCombined->ShallowCopy(wallAppender->GetOutput());
				SetSurfaceTypeField(wallCombined, false);
				SetRegionCountField(wallCombined, "RegionXletCount", xletCount);
				SetRegionCountField(wallCombined, "RegionWallGroupCount", wallCount);
				regionOut->SetBlock(outIdx, wallCombined);
				SetBlockName(regionOut, outIdx, "Walls");
				++unifiedWallRegions;
			}

			const std::string regionName = GetBlockName(regions, static_cast<unsigned int>(r));
			regions->SetBlock(static_cast<unsigned int>(r), regionOut);
			if (!regionName.empty()) {
				SetBlockName(regions, static_cast<unsigned int>(r), regionName);
			}
		}
	}

	std::cout << "Xlet/Wall summary: groups=" << totalGroups
			  << " xlets=" << totalXlets
			  << " walls=" << (unifyWalls ? unifiedWallRegions : totalWalls)
			  << std::endl;
}


static void SetBlockName(vtkMultiBlockDataSet* mb, unsigned int idx, const std::string& name) {
	if (!mb) return;
	vtkInformation* info = mb->GetMetaData(idx);
	info->Set(vtkCompositeDataSet::NAME(), name.c_str());
}

static inline int GetCellInt(vtkDataArray* arr, vtkIdType cellId) {
	return static_cast<int>(arr->GetComponent(cellId, 0));
}

static std::string JoinArrayNames(vtkDataSetAttributes* attrs) {
	if (!attrs) return "";
	std::ostringstream oss;
	for (int i = 0; i < attrs->GetNumberOfArrays(); ++i) {
		if (i > 0) oss << ", ";
		const char* name = attrs->GetArrayName(i);
		oss << (name ? name : "<unnamed>");
	}
	return oss.str();
}

static std::string JoinFieldArrayNames(vtkFieldData* attrs) {
	if (!attrs) return "";
	std::ostringstream oss;
	for (int i = 0; i < attrs->GetNumberOfArrays(); ++i) {
		if (i > 0) oss << ", ";
		const char* name = attrs->GetArrayName(i);
		oss << (name ? name : "<unnamed>");
	}
	return oss.str();
}

static void LogDataArrays(const std::string& label, vtkPolyData* pd) {
	if (!pd) return;
	std::cerr << "[" << label << "] CellData arrays: ["
			  << JoinArrayNames(pd->GetCellData()) << "] PointData arrays: ["
			  << JoinArrayNames(pd->GetPointData()) << "] FieldData arrays: ["
			  << JoinFieldArrayNames(pd->GetFieldData()) << "]\n";
}

static vtkSmartPointer<vtkIntArray> ConvertStringArrayToInt(vtkStringArray* strArr) {
	if (!strArr) return nullptr;
	auto out = vtkSmartPointer<vtkIntArray>::New();
	out->SetNumberOfComponents(1);
	out->SetNumberOfTuples(strArr->GetNumberOfTuples());

	std::unordered_map<std::string, int> mapping;
	int nextId = 0;
	for (vtkIdType i = 0; i < strArr->GetNumberOfTuples(); ++i) {
		const std::string value = strArr->GetValue(i);
		auto it = mapping.find(value);
		if (it == mapping.end()) {
			mapping.emplace(value, nextId);
			out->SetValue(i, nextId);
			++nextId;
		} else {
			out->SetValue(i, it->second);
		}
	}

	return out;
}

static vtkDataArray* ResolveGroupArrayOnCellData(
	vtkPolyData* pd,
	const std::vector<std::string>& candidates,
	std::string& resolvedName) {
	if (!pd) return nullptr;
	for (const auto& name : candidates) {
		if (name.empty()) continue;
		vtkAbstractArray* abs = pd->GetCellData()->GetAbstractArray(name.c_str());
		if (!abs) continue;
		if (auto* dataArr = vtkDataArray::SafeDownCast(abs)) {
			resolvedName = name;
			return dataArr;
		}
		if (auto* strArr = vtkStringArray::SafeDownCast(abs)) {
			auto conv = ConvertStringArrayToInt(strArr);
			if (conv) {
				conv->SetName(name.c_str());
				pd->GetCellData()->AddArray(conv);
				resolvedName = name;
				return conv;
			}
		}
	}
	return nullptr;
}

static vtkDataArray* ResolveGroupArrayOnPointData(
	vtkPolyData* pd,
	const std::vector<std::string>& candidates,
	std::string& resolvedName) {
	if (!pd) return nullptr;
	for (const auto& name : candidates) {
		if (name.empty()) continue;
		vtkAbstractArray* abs = pd->GetPointData()->GetAbstractArray(name.c_str());
		if (!abs) continue;
		if (auto* dataArr = vtkDataArray::SafeDownCast(abs)) {
			resolvedName = name;
			return dataArr;
		}
		if (auto* strArr = vtkStringArray::SafeDownCast(abs)) {
			auto conv = ConvertStringArrayToInt(strArr);
			if (conv) {
				conv->SetName(name.c_str());
				pd->GetPointData()->AddArray(conv);
				resolvedName = name;
				return conv;
			}
		}
	}
	return nullptr;
}

static bool PromoteGroupArrayFromFieldData(
	vtkPolyData* pd,
	const std::vector<std::string>& candidates,
	std::string& resolvedName) {
	if (!pd) return false;
	vtkFieldData* field = pd->GetFieldData();
	if (!field) return false;

	for (const auto& name : candidates) {
		if (name.empty()) continue;
		vtkAbstractArray* abs = field->GetAbstractArray(name.c_str());
		if (!abs) continue;
		vtkDataArray* dataArr = vtkDataArray::SafeDownCast(abs);
		if (!dataArr) continue;

		const vtkIdType tuples = dataArr->GetNumberOfTuples();
		if (tuples == pd->GetNumberOfCells()) {
			pd->GetCellData()->AddArray(dataArr);
			resolvedName = name;
			return true;
		}
		if (tuples == pd->GetNumberOfPoints()) {
			pd->GetPointData()->AddArray(dataArr);
			resolvedName = name;
			return true;
		}
	}

	return false;
}

static vtkDataArray* FindGroupArray(
	vtkPolyData* pd,
	const std::string& requestedName,
	std::string& resolvedName,
	bool& fromPointData) {
	if (!pd) return nullptr;

	std::vector<std::string> candidates = {
		requestedName,
		"GroupIds",
		"groupIds",
		"group_id",
		"groupId",
		"group_ids",
		"GroupID",
		"groupID"
	};

	if (auto arr = ResolveGroupArrayOnCellData(pd, candidates, resolvedName)) {
		fromPointData = false;
		return arr;
	}

	if (auto arr = ResolveGroupArrayOnPointData(pd, candidates, resolvedName)) {
		fromPointData = true;
		return arr;
	}

	resolvedName.clear();
	fromPointData = false;
	return nullptr;
}


vtkSmartPointer<vtkMultiBlockDataSet> BuildRegionSurfaceHierarchy(
	vtkMultiBlockDataSet* inputMb,
	const std::string& groupArrayName,
	const std::string& regionArrayName,
	bool addOriginalBlockId) {
	if (!inputMb) {
		throw std::runtime_error("BuildRegionSurfaceHierarchy: inputMb is null.");
	}

	auto appender = vtkSmartPointer<vtkAppendPolyData>::New();
	auto it = vtkSmartPointer<vtkCompositeDataIterator>::Take(inputMb->NewIterator());
	it->SkipEmptyNodesOn();

	int blockCounter = 0;
	for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem()) {
		vtkDataObject* dobj = it->GetCurrentDataObject();
		vtkPolyData* pd = vtkPolyData::SafeDownCast(dobj);
		if (!pd) continue;

		std::vector<std::string> candidates = {
			groupArrayName,
			"GroupIds",
			"groupIds",
			"group_id",
			"groupId",
			"group_ids",
			"GroupID",
			"groupID"
		};
		std::string promotedName;
		if (PromoteGroupArrayFromFieldData(pd, candidates, promotedName)) {
			std::cerr << "Info: promoted Group array '" << promotedName
					  << "' from FieldData on input block.\n";
		}

		auto tri = vtkSmartPointer<vtkTriangleFilter>::New();
		tri->SetInputData(pd);
		tri->Update();

		vtkPolyData* triPd = tri->GetOutput();
		if (!triPd || triPd->GetNumberOfCells() == 0) continue;

		const std::string cellNamesBefore = JoinArrayNames(triPd->GetCellData());
		const std::string pointNamesBefore = JoinArrayNames(triPd->GetPointData());
		const std::string fieldNamesBefore = JoinFieldArrayNames(triPd->GetFieldData());

		std::string resolvedGroupName;
		bool fromPointData = false;
		vtkDataArray* gArr = FindGroupArray(triPd, groupArrayName, resolvedGroupName, fromPointData);
		if (gArr && fromPointData) {
			auto p2c = vtkSmartPointer<vtkPointDataToCellData>::New();
			p2c->SetInputData(triPd);
			p2c->PassPointDataOn();
			p2c->Update();
			triPd = vtkPolyData::SafeDownCast(p2c->GetOutput());
			gArr = triPd ? triPd->GetCellData()->GetArray(resolvedGroupName.c_str()) : nullptr;
			fromPointData = false;
		}
		if (!gArr) {
			auto fallback = vtkSmartPointer<vtkIntArray>::New();
			fallback->SetName(groupArrayName.c_str());
			fallback->SetNumberOfComponents(1);
			fallback->SetNumberOfTuples(triPd->GetNumberOfCells());
			for (vtkIdType c = 0; c < triPd->GetNumberOfCells(); ++c) {
				fallback->SetValue(c, 0);
			}
			triPd->GetCellData()->AddArray(fallback);
			gArr = fallback;
			std::cerr << "Warning: missing GroupId array; defaulting all cells to GroupId=0. "
					  << "Available CellData arrays: [" << cellNamesBefore << "] "
					  << "Available PointData arrays: [" << pointNamesBefore << "] "
					  << "Available FieldData arrays: [" << fieldNamesBefore << "]\n";
		}
		if (gArr && !resolvedGroupName.empty() && resolvedGroupName != groupArrayName) {
			auto renamed = vtkSmartPointer<vtkDataArray>::Take(gArr->NewInstance());
			renamed->DeepCopy(gArr);
			renamed->SetName(groupArrayName.c_str());
			triPd->GetCellData()->AddArray(renamed);
			gArr = renamed;
		}
		if (!resolvedGroupName.empty() && resolvedGroupName != groupArrayName) {
			std::cerr << "Info: using GroupId array name '" << resolvedGroupName
					  << "' for block " << blockCounter << ".\n";
		}

		if (addOriginalBlockId) {
			auto ob = vtkSmartPointer<vtkIntArray>::New();
			ob->SetName("OriginalBlockId");
			ob->SetNumberOfComponents(1);
			ob->SetNumberOfTuples(triPd->GetNumberOfCells());
			for (vtkIdType c = 0; c < triPd->GetNumberOfCells(); ++c) {
				ob->SetValue(c, blockCounter);
			}
			triPd->GetCellData()->AddArray(ob);
		}

		appender->AddInputData(triPd);
		++blockCounter;
	}

	appender->Update();

	vtkPolyData* flatPd = appender->GetOutput();
	if (!flatPd || flatPd->GetNumberOfCells() == 0) {
		return vtkSmartPointer<vtkMultiBlockDataSet>::New();
	}

	auto conn = vtkSmartPointer<vtkConnectivityFilter>::New();
	conn->SetInputData(flatPd);
	conn->SetExtractionModeToAllRegions();
	conn->ColorRegionsOn();
	conn->Update();

	vtkPolyData* labeledPd = vtkPolyData::SafeDownCast(conn->GetOutput());
	if (!labeledPd || labeledPd->GetNumberOfCells() == 0) {
		return vtkSmartPointer<vtkMultiBlockDataSet>::New();
	}

	vtkDataArray* groupArr = labeledPd->GetCellData()->GetArray(groupArrayName.c_str());
	if (!groupArr) {
		std::ostringstream oss;
		oss << "After connectivity, missing CellData array '" << groupArrayName << "'.";
		throw std::runtime_error(oss.str());
	}

	vtkDataArray* regionArr = labeledPd->GetCellData()->GetArray(regionArrayName.c_str());
	if (!regionArr) {
		std::ostringstream oss;
		oss << "Missing CellData array '" << regionArrayName << "' produced by vtkConnectivityFilter.";
		throw std::runtime_error(oss.str());
	}

	std::map<int, std::map<int, vtkSmartPointer<vtkIdList>>> regionGroupToCells;

	const vtkIdType nCells = labeledPd->GetNumberOfCells();
	for (vtkIdType c = 0; c < nCells; ++c) {
		const int r = GetCellInt(regionArr, c);
		const int g = GetCellInt(groupArr, c);

		auto& groupMap = regionGroupToCells[r];
		auto& idList = groupMap[g];

		if (!idList) {
			idList = vtkSmartPointer<vtkIdList>::New();
		}
		idList->InsertNextId(c);
	}

	auto out = vtkSmartPointer<vtkMultiBlockDataSet>::New();
	out->SetNumberOfBlocks(static_cast<unsigned int>(regionGroupToCells.size()));

	unsigned int regionIdx = 0;
	for (const auto& regionPair : regionGroupToCells) {
		const int regionId = regionPair.first;
		const auto& groupMap = regionPair.second;

		auto regionMb = vtkSmartPointer<vtkMultiBlockDataSet>::New();
		regionMb->SetNumberOfBlocks(static_cast<unsigned int>(groupMap.size()));

		std::ostringstream regionName;
		regionName << "Region_" << regionId;
		SetBlockName(out, regionIdx, regionName.str());

		unsigned int groupIdx = 0;
		for (const auto& groupPair : groupMap) {
			const int groupId = groupPair.first;
			const auto& cellIds = groupPair.second;

			auto extract = vtkSmartPointer<vtkExtractCells>::New();
			extract->SetInputData(labeledPd);
			extract->SetCellList(cellIds);
			extract->Update();

			auto geom = vtkSmartPointer<vtkGeometryFilter>::New();
			geom->SetInputConnection(extract->GetOutputPort());
			geom->Update();

			vtkPolyData* leaf = geom->GetOutput();
			auto leafCopy = vtkSmartPointer<vtkPolyData>::New();
			leafCopy->ShallowCopy(leaf);

			regionMb->SetBlock(groupIdx, leafCopy);

			std::ostringstream groupName;
			groupName << "Group_" << groupId;
			SetBlockName(regionMb, groupIdx, groupName.str());

			++groupIdx;
		}

		out->SetBlock(regionIdx, regionMb);
		++regionIdx;
	}

	return out;
}

} // namespace fastvessels
