#pragma once

#include "fastvessels/config.hpp"
#include "fastvessels/solver.hpp"

#include <vector>

namespace fastvessels {

struct PreprocessResult {
	SolverConfig best_config;
	BenchmarkResult best_result;
	std::vector<BenchmarkResult> all_results;
};

PreprocessResult RunPreprocess(const SolverConfig& base, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi);

} // namespace fastvessels
