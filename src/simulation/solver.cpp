#include "fastvessels/solver.hpp"

#include "fastvessels/common.hpp"
#include "fastvessels/io_utils.hpp"
#include "fastvessels/obj_pipeline.hpp"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <limits>
#include <thread>
#include <vector>

namespace fastvessels {

static BenchmarkResult RunBenchmarkOnce(const SolverConfig& config, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi) {
	BenchmarkResult result;
	result.feature = config.feature;
	result.work_items = config.work_items;
	result.inner_iters = config.inner_iters;

	const auto& featureSpec = ResolveFeatureSpec(config.feature);
	int detectedGpus = gpu.count;
	int requestedGpus = config.num_gpus;
	int useGpus = requestedGpus;
	if (useGpus < 0) {
		if (config.default_num_gpus >= 0) {
			useGpus = config.default_num_gpus;
		} else {
			useGpus = detectedGpus;
		}
	}
	useGpus = std::max(0, useGpus);

	int cpuThreads = config.cpu_threads;
	if (cpuThreads <= 0) {
		if (config.default_cpu_threads > 0) {
			cpuThreads = config.default_cpu_threads;
		} else {
			cpuThreads = static_cast<int>(std::max(1u, cpu.logical));
		}
	}
	cpuThreads = std::max(1, cpuThreads);

	const auto& resolvedFeature = (featureSpec.name == "default" && !config.default_feature.empty())
		? ResolveFeatureSpec(config.default_feature)
		: featureSpec;
	bool wantsGpu = resolvedFeature.uses_gpu;
	bool wantsCpuAccel = resolvedFeature.uses_cpu;
	if (resolvedFeature.name == "default") {
		cpuThreads = 1;
		useGpus = 0;
	}
	if (!wantsCpuAccel) {
		cpuThreads = 1;
	}
	if (!wantsGpu) {
		useGpus = 0;
	}
	if (wantsGpu) {
		cpuThreads = std::max(cpuThreads, config.min_cpu_for_gpu);
	}

	result.cpu_threads = cpuThreads;
	result.gpu_workers = useGpus;

	auto start = std::chrono::steady_clock::now();

	auto blocks = ReadGeometry_AsMultiBlock(config.obj_path);
	auto refined = BuildRegionSurfaceHierarchy(blocks, "GroupId", "RegionId", true);

	std::filesystem::create_directories("output");
	WriteMultiBlock(refined, "output/geometry_multiblock.vtm");

	ObjPipelineStats stats = AnalyzeClosedSurfaces(refined, cpuThreads);

	auto stop = std::chrono::steady_clock::now();
	result.seconds = std::chrono::duration<double>(stop - start).count();
	result.block_count = stats.block_count;
	result.closed_surfaces = stats.closed_surfaces;
	result.blocks_with_openings = stats.blocks_with_openings;
	result.boundary_edge_cells = stats.boundary_edge_cells;
	result.checksum = static_cast<double>(stats.closed_surfaces);
	return result;
}

BenchmarkResult RunBenchmark(const SolverConfig& config, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi) {
	BenchmarkResult best;
	best.seconds = std::numeric_limits<double>::infinity();
	for (int r = 0; r < config.repetitions; ++r) {
		auto res = RunBenchmarkOnce(config, cpu, gpu, mpi);
		if (res.seconds < best.seconds) {
			best = res;
		}
	}
	return best;
}

double ThroughputItemsPerSecond(const BenchmarkResult& r) {
	if (r.seconds <= 0.0) {
		return 0.0;
	}
	if (r.block_count > 0) {
		return static_cast<double>(r.block_count) / r.seconds;
	}
	return static_cast<double>(r.work_items) / r.seconds;
}

} // namespace fastvessels
