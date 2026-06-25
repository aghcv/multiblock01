#include "voxeliser/voxeliser.hpp"

#include <vtkAppendPolyData.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStaticCellLocator.h>
#include <vtkTriangleFilter.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

#ifdef MULTIBLOCK_ENABLE_VDB
#include <openvdb/io/File.h>
#include <openvdb/tools/MeshToVolume.h>
#endif

#ifdef MULTIBLOCK_ENABLE_NANOVDB
#include "voxeliser/nanovdb_utils.hpp"
#endif

namespace voxeliser {

namespace {

int GetFieldInt(vtkPolyData* pd, const std::string& name, int defaultValue) {
	if (!pd) return defaultValue;
	vtkFieldData* field = pd->GetFieldData();
	if (!field) return defaultValue;
	auto* arr = vtkIntArray::SafeDownCast(field->GetAbstractArray(name.c_str()));
	if (!arr || arr->GetNumberOfTuples() < 1) return defaultValue;
	return arr->GetValue(0);
}

vtkSmartPointer<vtkIntArray> EnsureCellIntArray(
	vtkPolyData* pd,
	const std::string& name,
	int defaultValue) {
	if (!pd) return nullptr;
	vtkDataArray* existing = pd->GetCellData()->GetArray(name.c_str());
	if (existing) {
		return vtkIntArray::SafeDownCast(existing);
	}
	auto arr = vtkSmartPointer<vtkIntArray>::New();
	arr->SetName(name.c_str());
	arr->SetNumberOfComponents(1);
	arr->SetNumberOfTuples(pd->GetNumberOfCells());
	for (vtkIdType i = 0; i < pd->GetNumberOfCells(); ++i) {
		arr->SetValue(i, defaultValue);
	}
	pd->GetCellData()->AddArray(arr);
	return arr;
}

vtkSmartPointer<vtkPolyData> Triangulate(vtkPolyData* input) {
	auto tri = vtkSmartPointer<vtkTriangleFilter>::New();
	tri->SetInputData(input);
	tri->Update();
	auto out = vtkSmartPointer<vtkPolyData>::New();
	out->ShallowCopy(tri->GetOutput());
	return out;
}

#ifdef MULTIBLOCK_ENABLE_VDB

struct TriangleMesh {
	std::vector<openvdb::Vec3s> points;
	std::vector<openvdb::Vec3I> triangles;
};

TriangleMesh BuildTriangleMesh(vtkPolyData* poly) {
	TriangleMesh mesh;
	if (!poly) return mesh;

	const vtkIdType numPoints = poly->GetNumberOfPoints();
	mesh.points.reserve(static_cast<size_t>(numPoints));
	for (vtkIdType i = 0; i < numPoints; ++i) {
		double p[3] = {0.0, 0.0, 0.0};
		poly->GetPoint(i, p);
		mesh.points.emplace_back(static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]));
	}

	const vtkIdType numCells = poly->GetNumberOfCells();
	mesh.triangles.reserve(static_cast<size_t>(numCells));
	for (vtkIdType c = 0; c < numCells; ++c) {
		vtkCell* cell = poly->GetCell(c);
		if (!cell || cell->GetNumberOfPoints() != 3) continue;
		vtkIdList* ids = cell->GetPointIds();
		mesh.triangles.emplace_back(
			static_cast<int>(ids->GetId(0)),
			static_cast<int>(ids->GetId(1)),
			static_cast<int>(ids->GetId(2)));
	}
	return mesh;
}

#endif

} // namespace

vtkSmartPointer<vtkPolyData> BuildLabeledSurfacePolyData(
	vtkMultiBlockDataSet* regions,
	const VoxeliserConfig& config) {
	if (!regions) {
		return nullptr;
	}

	auto appender = vtkSmartPointer<vtkAppendPolyData>::New();

	const unsigned int regionCount = regions->GetNumberOfBlocks();
	for (unsigned int r = 0; r < regionCount; ++r) {
		auto* regionMb = vtkMultiBlockDataSet::SafeDownCast(regions->GetBlock(r));
		if (!regionMb) continue;
		const unsigned int groupCount = regionMb->GetNumberOfBlocks();
		for (unsigned int g = 0; g < groupCount; ++g) {
			auto* pd = vtkPolyData::SafeDownCast(regionMb->GetBlock(g));
			if (!pd || pd->GetNumberOfCells() == 0) continue;

			auto copy = vtkSmartPointer<vtkPolyData>::New();
			copy->ShallowCopy(pd);

			const int surfaceTypeValue = GetFieldInt(copy, config.surfaceTypeArrayName, 0);
			EnsureCellIntArray(copy, config.surfaceTypeArrayName, surfaceTypeValue);

			if (!copy->GetCellData()->GetArray(config.regionArrayName.c_str())) {
				std::cerr << "Warning: missing cell array '" << config.regionArrayName
						  << "' on a surface block.\n";
			}
			if (!copy->GetCellData()->GetArray(config.groupArrayName.c_str())) {
				std::cerr << "Warning: missing cell array '" << config.groupArrayName
						  << "' on a surface block.\n";
			}

			appender->AddInputData(copy);
		}
	}

	appender->Update();
	auto out = vtkSmartPointer<vtkPolyData>::New();
	out->ShallowCopy(appender->GetOutput());
	return out;
}

VoxelisedResult VoxelisePolyData(vtkPolyData* poly, const VoxeliserConfig& config) {
	VoxelisedResult out;
	if (!poly) {
		throw std::runtime_error("VoxelisePolyData: input polydata is null.");
	}

#ifdef MULTIBLOCK_ENABLE_VDB
	static bool vdbInit = false;
	if (!vdbInit) {
		openvdb::initialize();
		vdbInit = true;
	}

	auto triPd = Triangulate(poly);
	if (!triPd || triPd->GetNumberOfCells() == 0) {
		throw std::runtime_error("VoxelisePolyData: triangulated mesh is empty.");
	}

	TriangleMesh mesh = BuildTriangleMesh(triPd);
	if (mesh.triangles.empty()) {
		throw std::runtime_error("VoxelisePolyData: no triangles produced.");
	}

	auto transform = openvdb::math::Transform::createLinearTransform(config.voxelSize);
	std::vector<openvdb::Vec4I> quads;
	out.sdf = openvdb::tools::meshToSignedDistanceField<openvdb::FloatGrid>(
		*transform, mesh.points, mesh.triangles, quads,
		config.exteriorBandwidth, config.interiorBandwidth);
	out.sdf->setName("sdf");

	out.region = openvdb::Int32Grid::create(-1);
	out.group = openvdb::Int32Grid::create(-1);
	out.surfaceType = openvdb::Int32Grid::create(-1);

	out.region->setTransform(out.sdf->transform().copy());
	out.group->setTransform(out.sdf->transform().copy());
	out.surfaceType->setTransform(out.sdf->transform().copy());

	out.region->setName(config.regionArrayName);
	out.group->setName(config.groupArrayName);
	out.surfaceType->setName(config.surfaceTypeArrayName);

	auto* regionArr = triPd->GetCellData()->GetArray(config.regionArrayName.c_str());
	auto* groupArr = triPd->GetCellData()->GetArray(config.groupArrayName.c_str());
	auto* surfaceArr = triPd->GetCellData()->GetArray(config.surfaceTypeArrayName.c_str());

	auto locator = vtkSmartPointer<vtkStaticCellLocator>::New();
	locator->SetDataSet(triPd);
	locator->BuildLocator();

	const float surfaceIso = config.surfaceBandVoxels * config.voxelSize;
	for (auto iter = out.sdf->cbeginValueOn(); iter; ++iter) {
		const openvdb::Coord ijk = iter.getCoord();
		const openvdb::Vec3d world = out.sdf->transform().indexToWorld(ijk);
		double query[3] = {world.x(), world.y(), world.z()};
		double closest[3] = {0.0, 0.0, 0.0};
		vtkIdType cellId = 0;
		int subId = 0;
		double dist2 = 0.0;
		locator->FindClosestPoint(query, closest, cellId, subId, dist2);

		int region = regionArr ? static_cast<int>(regionArr->GetComponent(cellId, 0)) : 0;
		int group = groupArr ? static_cast<int>(groupArr->GetComponent(cellId, 0)) : 0;
		int surface = surfaceArr ? static_cast<int>(surfaceArr->GetComponent(cellId, 0)) : 0;

		out.region->tree().setValueOn(ijk, region);
		out.group->tree().setValueOn(ijk, group);
		out.surfaceType->tree().setValueOn(ijk, surface);

		if (std::abs(iter.getValue()) <= surfaceIso) {
			out.surfaceVoxels.push_back({ijk.x(), ijk.y(), ijk.z()});
		}
	}

	return out;
#else
	(void)config;
	throw std::runtime_error("VoxelisePolyData: OpenVDB support is disabled in this build.");
#endif
}

bool ExportVdbGrids(const VoxelisedResult& result, const std::string& path) {
#ifdef MULTIBLOCK_ENABLE_VDB
	if (path.empty()) {
		return false;
	}
	openvdb::GridPtrVec grids;
	if (result.sdf) grids.push_back(result.sdf);
	if (result.region) grids.push_back(result.region);
	if (result.group) grids.push_back(result.group);
	if (result.surfaceType) grids.push_back(result.surfaceType);
	if (grids.empty()) {
		return false;
	}
	openvdb::io::File file(path);
	file.write(grids);
	file.close();
	return true;
#else
	(void)result;
	(void)path;
	return false;
#endif
}

#ifdef MULTIBLOCK_ENABLE_NANOVDB

NanoVdbGrids ConvertToNanoVdb(const VoxelisedResult& result) {
	NanoVdbGrids out;
#ifdef MULTIBLOCK_ENABLE_VDB
	if (result.sdf) {
		out.sdf = nanovdb::tools::createNanoGrid(*result.sdf);
	}
	if (result.region) {
		out.region = nanovdb::tools::createNanoGrid(*result.region);
	}
	if (result.group) {
		out.group = nanovdb::tools::createNanoGrid(*result.group);
	}
	if (result.surfaceType) {
		out.surfaceType = nanovdb::tools::createNanoGrid(*result.surfaceType);
	}
#else
	(void)result;
#endif
	return out;
}

bool ExportNanoVdbGrids(const NanoVdbGrids& grids, const std::string& path) {
	return WriteNanoVdbGrids(grids, path);
}

#endif

} // namespace voxeliser
