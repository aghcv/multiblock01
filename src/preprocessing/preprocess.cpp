#include "fastvessels/preprocess.hpp"

#include "fastvessels/common.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <thread>
#include <vector>

namespace fastvessels {

static std::vector<int> SuggestCounts(unsigned int maxCount) {
	std::vector<int> counts;
	if (maxCount == 0) {
		return counts;
	}
	for (unsigned int v = 1; v <= maxCount; v <<= 1) {
		counts.push_back(static_cast<int>(v));
	}
	if (counts.back() != static_cast<int>(maxCount)) {
		counts.push_back(static_cast<int>(maxCount));
	}
	return counts;
}

static SolverConfig MakeCandidateConfig(const SolverConfig& base, const std::string& feature, int cpuThreads, int gpus) {
	SolverConfig cfg = base;
	cfg.mode = "run";
	cfg.feature = feature;
	cfg.cpu_threads = cpuThreads;
	cfg.num_gpus = gpus;
	return cfg;
}

static BenchmarkResult RunKernelBenchmarkOnce(const SolverConfig& config, int cpuThreads, int useGpus) {
	BenchmarkResult result;
	result.feature = config.feature;
	result.work_items = config.work_items;
	result.inner_iters = config.inner_iters;
	result.cpu_threads = cpuThreads;
	result.gpu_workers = useGpus;

	std::uint64_t totalItems = config.work_items;
	double gpuFraction = std::clamp(config.gpu_work_fraction, 0.0, 1.0);
	std::uint64_t gpuItems = 0;
	if (useGpus > 0) {
		gpuItems = static_cast<std::uint64_t>(static_cast<long double>(totalItems) * gpuFraction);
	}
	std::uint64_t cpuItems = totalItems - gpuItems;

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
		workers.emplace_back(runRange, w, begin, end, 1'000'000'000ULL);
	}

	if (useGpus > 0) {
		for (int g = 0; g < useGpus; ++g) {
			std::uint64_t begin = SplitWorkBegin(gpuItems, g, useGpus);
			std::uint64_t end = SplitWorkEnd(gpuItems, g, useGpus);
			workers.emplace_back(runRange, cpuWorkerCount + g, cpuItems + begin, cpuItems + end, 1'000'000'000ULL + 12345ULL);
		}
	}

	for (auto& w : workers) {
		w.join();
	}

	auto stop = std::chrono::steady_clock::now();
	result.seconds = std::chrono::duration<double>(stop - start).count();
	result.checksum = 0.0;
	for (double v : partialSums) {
		result.checksum += v;
	}
	return result;
}

static BenchmarkResult RunKernelBenchmark(const SolverConfig& config, int cpuThreads, int useGpus) {
	BenchmarkResult best;
	best.seconds = std::numeric_limits<double>::infinity();
	for (int r = 0; r < config.repetitions; ++r) {
		auto res = RunKernelBenchmarkOnce(config, cpuThreads, useGpus);
		if (res.seconds < best.seconds) {
			best = res;
		}
	}
	return best;
}

PreprocessResult RunPreprocess(const SolverConfig& base, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi) {
	PreprocessResult out;
	out.best_config = base;
	out.best_config.mode = "run";

	if (mpi.rank != 0) {
		return out;
	}

	std::vector<int> cpuCandidates = SuggestCounts(cpu.logical);
	int detectedGpus = gpu.count;
	std::vector<int> gpuCandidates;
	if (detectedGpus > 0) {
		for (int g = 0; g <= detectedGpus; ++g) {
			gpuCandidates.push_back(g);
		}
	} else {
		gpuCandidates.push_back(0);
	}

	out.best_result.seconds = std::numeric_limits<double>::infinity();

	auto consider = [&](const SolverConfig& candidate, int cpuThreads, int gpus) {
		auto res = RunKernelBenchmark(candidate, cpuThreads, gpus);
		out.all_results.push_back(res);
		if (res.seconds < out.best_result.seconds) {
			out.best_result = res;
			out.best_config = candidate;
			out.best_config.default_cpu_threads = cpuThreads;
			out.best_config.default_num_gpus = gpus;
			out.best_config.default_feature = candidate.feature;
		}
	};

	for (const auto& spec : GetFeatureSpecs()) {
		if (!spec.uses_cpu && !spec.uses_gpu) {
			consider(MakeCandidateConfig(base, spec.name, 1, 0), 1, 0);
			continue;
		}
		if (spec.uses_gpu && detectedGpus <= 0) {
			continue;
		}
		if (spec.uses_gpu) {
			for (int g : gpuCandidates) {
				if (g <= 0) continue;
				if (spec.uses_cpu) {
					for (int t : cpuCandidates) {
						consider(MakeCandidateConfig(base, spec.name, t, g), t, g);
					}
				} else {
					int t = std::max(1, base.min_cpu_for_gpu);
					consider(MakeCandidateConfig(base, spec.name, t, g), t, g);
				}
			}
			continue;
		}
		for (int t : cpuCandidates) {
			consider(MakeCandidateConfig(base, spec.name, t, 0), t, 0);
		}
	}

	return out;
}

} // namespace fastvessels
