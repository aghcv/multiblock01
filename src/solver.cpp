#include "fastvessels/solver.hpp"

#include "fastvessels/common.hpp"

#include <algorithm>
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
		useGpus = detectedGpus;
	}
	useGpus = std::max(0, useGpus);

	int cpuThreads = config.cpu_threads;
	if (cpuThreads <= 0) {
		cpuThreads = static_cast<int>(std::max(1u, cpu.logical));
	}
	cpuThreads = std::max(1, cpuThreads);

	bool wantsGpu = featureSpec.uses_gpu;
	bool wantsCpuAccel = featureSpec.uses_cpu;
	if (featureSpec.name == "default") {
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

	std::uint64_t rankBegin = SplitWorkBegin(config.work_items, mpi.rank, mpi.size);
	std::uint64_t rankEnd = SplitWorkEnd(config.work_items, mpi.rank, mpi.size);
	std::uint64_t rankItems = rankEnd - rankBegin;

	double gpuFraction = std::clamp(config.gpu_work_fraction, 0.0, 1.0);
	std::uint64_t gpuItems = 0;
	if (useGpus > 0) {
		gpuItems = static_cast<std::uint64_t>(static_cast<long double>(rankItems) * gpuFraction);
	}
	std::uint64_t cpuItems = rankItems - gpuItems;

	int cpuWorkerCount = std::max(1, cpuThreads);
	int totalWorkers = cpuWorkerCount + std::max(0, useGpus);
	std::vector<double> partialSums(static_cast<size_t>(totalWorkers), 0.0);
	auto start = std::chrono::steady_clock::now();

	std::vector<std::thread> workers;
	workers.reserve(static_cast<size_t>(totalWorkers));

	auto runRange = [&](int workerIndex, std::uint64_t begin, std::uint64_t end, std::uint64_t seedBase) {
		double local = 0.0;
		for (std::uint64_t i = begin; i < end; ++i) {
			local += ComputeKernel(seedBase + i, config.inner_iters);
		}
		partialSums[static_cast<size_t>(workerIndex)] = local;
	};

	for (int w = 0; w < cpuWorkerCount; ++w) {
		std::uint64_t begin = SplitWorkBegin(cpuItems, w, cpuWorkerCount);
		std::uint64_t end = SplitWorkEnd(cpuItems, w, cpuWorkerCount);
		workers.emplace_back(runRange, w, rankBegin + begin, rankBegin + end,
			static_cast<std::uint64_t>(mpi.rank) * 1'000'000'000ULL);
	}

	if (useGpus > 0) {
		for (int g = 0; g < useGpus; ++g) {
			std::uint64_t begin = SplitWorkBegin(gpuItems, g, useGpus);
			std::uint64_t end = SplitWorkEnd(gpuItems, g, useGpus);
			workers.emplace_back(runRange, cpuWorkerCount + g, rankBegin + cpuItems + begin, rankBegin + cpuItems + end,
				static_cast<std::uint64_t>(mpi.rank) * 1'000'000'000ULL + 12345ULL);
		}
	}

	for (auto& t : workers) {
		t.join();
	}

	auto stop = std::chrono::steady_clock::now();
	result.seconds = std::chrono::duration<double>(stop - start).count();
	result.checksum = 0.0;
	for (double v : partialSums) {
		result.checksum += v;
	}
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
	return static_cast<double>(r.work_items) / r.seconds;
}

} // namespace fastvessels
