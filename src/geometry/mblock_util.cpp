#include "fastvessels/obj_pipeline.hpp"

#include "fastvessels/common.hpp"

#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkAppendPolyData.h>
#include <vtkAbstractArray.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkConnectivityFilter.h>
#include <vtkCellData.h>
#include <vtkExtractCells.h>
#include <vtkFeatureEdges.h>
#include <vtkGeometryFilter.h>
#include <vtkInformation.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkMapper.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkOBJImporter.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTriangleFilter.h>

#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vtkPointData.h>
#include <vtkPointDataToCellData.h>
#include <vector>

namespace fastvessels {

static std::string GuessMTLPathFromOBJ(const std::string& objPath) {
	namespace fs = std::filesystem;
	fs::path p(objPath);
	fs::path mtl = p;
	mtl.replace_extension(".mtl");
	if (fs::exists(mtl)) return mtl.string();
	return "";
}

static std::string JoinArrayNames(vtkDataSetAttributes* attrs);
static std::string JoinFieldArrayNames(vtkFieldData* attrs);
static void LogDataArrays(const std::string& label, vtkPolyData* pd);
static bool PromoteGroupArrayFromFieldData(
	vtkPolyData* pd,
	const std::vector<std::string>& candidates,
	std::string& resolvedName);

vtkSmartPointer<vtkMultiBlockDataSet> ReadGeometry_AsMultiBlock(const std::string& path) {
	namespace fs = std::filesystem;

	if (!fs::exists(path)) {
		throw std::runtime_error("Geometry file not found: " + path);
	}

	std::string ext = fs::path(path).extension().string();
	for (auto& c : ext) c = static_cast<char>(std::tolower(c));

	if (ext == ".vtp") {
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

	if (ext != ".obj") {
		throw std::runtime_error("Unsupported geometry format: " + ext);
	}

	auto importer = vtkSmartPointer<vtkOBJImporter>::New();
	importer->SetFileName(path.c_str());

	std::string mtlPath = GuessMTLPathFromOBJ(path);
	if (!mtlPath.empty()) {
		importer->SetFileNameMTL(mtlPath.c_str());
		fs::path texDir = fs::path(path).parent_path();
		importer->SetTexturePath(texDir.string().c_str());
	}

	importer->Update();

	vtkRenderer* ren = importer->GetRenderer();
	if (!ren) {
		throw std::runtime_error("OBJ importer did not produce a renderer.");
	}

	auto blocks = vtkSmartPointer<vtkMultiBlockDataSet>::New();

	vtkActorCollection* actors = ren->GetActors();
	actors->InitTraversal();

	unsigned int blockIdx = 0;
	for (vtkActor* a = actors->GetNextActor(); a != nullptr; a = actors->GetNextActor()) {
		vtkMapper* mapper = a->GetMapper();
		if (!mapper) continue;
		vtkPolyData* pd = vtkPolyData::SafeDownCast(mapper->GetInput());
		if (!pd || pd->GetNumberOfPoints() == 0) continue;

		auto copy = vtkSmartPointer<vtkPolyData>::New();
		copy->ShallowCopy(pd);

		blocks->SetBlock(blockIdx, copy);

		std::string name = a->GetObjectName();
		if (name.empty()) {
			name = "part_" + std::to_string(blockIdx);
		}
		blocks->GetMetaData(blockIdx)->Set(vtkCompositeDataSet::NAME(), name.c_str());

		++blockIdx;
	}

	if (blockIdx == 0) {
		throw std::runtime_error("No polydata blocks extracted from OBJ. Check OBJ/MTL integrity.");
	}

	blocks->SetNumberOfBlocks(blockIdx);
	return blocks;
}

static long long CountBoundaryEdges(vtkPolyData* pd) {
	auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
	edges->SetInputData(pd);
	edges->BoundaryEdgesOn();
	edges->FeatureEdgesOff();
	edges->ManifoldEdgesOff();
	edges->NonManifoldEdgesOff();
	edges->Update();
	return static_cast<long long>(edges->GetOutput()->GetNumberOfCells());
}

ObjPipelineStats AnalyzeClosedSurfaces(vtkMultiBlockDataSet* blocks, int cpuThreads) {
	ObjPipelineStats stats;
	if (!blocks) {
		return stats;
	}
	int blockCount = static_cast<int>(blocks->GetNumberOfBlocks());
	stats.block_count = blockCount;
	if (blockCount <= 0) {
		return stats;
	}

	int threads = std::max(1, cpuThreads);
	threads = std::min(threads, blockCount);
	std::vector<int> localClosed(static_cast<size_t>(threads), 0);
	std::vector<int> localBlocksWithOpenings(static_cast<size_t>(threads), 0);
	std::vector<long long> localBoundaryCells(static_cast<size_t>(threads), 0);
	std::vector<std::thread> workers;
	workers.reserve(static_cast<size_t>(threads));

	auto workerFn = [&](int workerIndex, int begin, int end) {
		int closed = 0;
		int openingsBlocks = 0;
		long long boundaryCells = 0;
		for (int i = begin; i < end; ++i) {
			vtkPolyData* pd = vtkPolyData::SafeDownCast(blocks->GetBlock(static_cast<unsigned int>(i)));
			if (!pd) continue;
			long long b = CountBoundaryEdges(pd);
			if (b == 0) {
				closed++;
			} else {
				openingsBlocks++;
				boundaryCells += b;
			}
		}
		localClosed[static_cast<size_t>(workerIndex)] = closed;
		localBlocksWithOpenings[static_cast<size_t>(workerIndex)] = openingsBlocks;
		localBoundaryCells[static_cast<size_t>(workerIndex)] = boundaryCells;
	};

	int base = blockCount / threads;
	int rem = blockCount % threads;
	int start = 0;
	for (int t = 0; t < threads; ++t) {
		int count = base + (t < rem ? 1 : 0);
		int end = start + count;
		workers.emplace_back(workerFn, t, start, end);
		start = end;
	}

	for (auto& w : workers) {
		w.join();
	}

	int closedTotal = 0;
	int openingsBlocksTotal = 0;
	long long boundaryCellsTotal = 0;
	for (size_t i = 0; i < localClosed.size(); ++i) {
		closedTotal += localClosed[i];
		openingsBlocksTotal += localBlocksWithOpenings[i];
		boundaryCellsTotal += localBoundaryCells[i];
	}
	stats.closed_surfaces = closedTotal;
	stats.blocks_with_openings = openingsBlocksTotal;
	stats.boundary_edge_cells = boundaryCellsTotal;
	return stats;
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
