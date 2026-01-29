#include "fastvessels/postprocess.hpp"

#include "fastvessels/common.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>

namespace fastvessels {

void WriteResults(const std::string& path,
	const SolverConfig& config,
	const CpuInfo& cpu,
	const GpuInfo& gpu,
	const MpiInfo& mpi,
	const BenchmarkResult& result) {
	if (mpi.rank != 0) {
		return;
	}
	std::filesystem::path outPath(path);
	if (outPath.has_parent_path()) {
		std::filesystem::create_directories(outPath.parent_path());
	}
	std::ofstream out(path);
	if (!out) {
		return;
	}

	out << "# Results\n";
	out << "feature=" << config.feature << "\n";
	out << "mode=" << config.mode << "\n";
	out << "cpu_logical=" << cpu.logical << "\n";
	if (cpu.physical > 0) {
		out << "cpu_physical=" << cpu.physical << "\n";
	}
	out << "gpus=" << gpu.count << "\n";
	if (gpu.cores > 0) {
		out << "gpu_cores=" << gpu.cores << "\n";
	}

	out << "work_items=" << result.work_items << "\n";
	out << "inner_iters=" << result.inner_iters << "\n";
	out << "cpu_threads=" << result.cpu_threads << "\n";
	out << "num_gpus=" << result.gpu_workers << "\n";
	out << "obj_path=" << config.obj_path << "\n";
	out << "blocks=" << result.block_count << "\n";
	out << "closed_surfaces=" << result.closed_surfaces << "\n";
	out << "blocks_with_openings=" << result.blocks_with_openings << "\n";
	out << "boundary_edge_cells=" << result.boundary_edge_cells << "\n";
	out << "time_s=" << std::fixed << std::setprecision(6) << result.seconds << "\n";
	out << "blocks_per_s=" << std::fixed << std::setprecision(2) << ThroughputItemsPerSecond(result) << "\n";
	out << "checksum=" << std::setprecision(6) << result.checksum << "\n";
}

} // namespace fastvessels
