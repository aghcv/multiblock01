#include "fastvessels/preprocess.hpp"

#include "fastvessels/common.hpp"

#include <fstream>
#include <iomanip>
#include <limits>

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

	auto consider = [&](const SolverConfig& candidate) {
		auto res = RunBenchmark(candidate, cpu, gpu, MpiInfo{0, 1, false});
		out.all_results.push_back(res);
		if (res.seconds < out.best_result.seconds) {
			out.best_result = res;
			out.best_config = candidate;
		}
	};

	for (const auto& spec : GetFeatureSpecs()) {
		if (!spec.uses_cpu && !spec.uses_gpu) {
			consider(MakeCandidateConfig(base, spec.name, 1, 0));
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
						consider(MakeCandidateConfig(base, spec.name, t, g));
					}
				} else {
					consider(MakeCandidateConfig(base, spec.name, std::max(1, base.min_cpu_for_gpu), g));
				}
			}
			continue;
		}
		for (int t : cpuCandidates) {
			consider(MakeCandidateConfig(base, spec.name, t, 0));
		}
	}

	return out;
}

} // namespace fastvessels
