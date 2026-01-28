#pragma once

#include "fastvessels/config.hpp"
#include "fastvessels/env_info.hpp"
#include "fastvessels/solver.hpp"

namespace fastvessels {

void WriteResults(const std::string& path,
	const SolverConfig& config,
	const CpuInfo& cpu,
	const GpuInfo& gpu,
	const MpiInfo& mpi,
	const BenchmarkResult& result);

} // namespace fastvessels
