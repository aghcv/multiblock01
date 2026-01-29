#pragma once

#include "fastvessels/config.hpp"
#include "fastvessels/env_info.hpp"

#include <cstdint>
#include <string>

namespace fastvessels {

struct BenchmarkResult {
	double seconds = 0.0;
	double checksum = 0.0;
	std::uint64_t work_items = 0;
	std::uint32_t inner_iters = 0;
	int cpu_threads = 1;
	int gpu_workers = 0;
	int block_count = 0;
	int closed_surfaces = 0;
	int blocks_with_openings = 0;
	long long boundary_edge_cells = 0;
	std::string feature;
};

BenchmarkResult RunBenchmark(const SolverConfig& config, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi);

double ThroughputItemsPerSecond(const BenchmarkResult& r);

} // namespace fastvessels
