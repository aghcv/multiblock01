#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace {

struct CpuInfo {
	unsigned int logical = 1;
	unsigned int physical = 0;
};

struct GpuInfo {
	int count = 0;
	int cores = 0;
	std::string name;
	std::string note;
};

struct MpiInfo {
	int rank = 0;
	int size = 1;
	bool active = false;
};

struct SolverConfig {
	std::string mode = "run"; // run|preprocess
	std::string feature = "default"; // default|cpu_accelerated|gpu_accelerated|cpu_gpu_accelerated

	std::string config_path = "solver_input.txt";
	std::string solver_file = "solver_input.txt";
	std::string optimized_output = "solver_input.optimized.txt";

	std::string use_mpi = "auto"; // auto|true|false

	int cpu_threads = 0; // 0=auto, else fixed per-rank
	int min_cpu_for_gpu = 1;
	int num_gpus = -1; // -1=auto-detect, 0=force CPU-only
	double gpu_work_fraction = 0.75; // fraction of work assigned to "GPU" workers when enabled

	std::uint64_t work_items = 5'000'000;
	std::uint32_t inner_iters = 64;
	int repetitions = 3;
};

static std::string ToLower(std::string value);

struct FeatureSpec {
	std::string name;
	bool uses_cpu = false;
	bool uses_gpu = false;
};

static const std::vector<FeatureSpec>& GetFeatureSpecs() {
	static const std::vector<FeatureSpec> specs = {
		{"default", false, false},
		{"cpu_accelerated", true, false},
		{"gpu_accelerated", false, true},
		{"cpu_gpu_accelerated", true, true},
	};
	return specs;
}

static const FeatureSpec& DefaultFeatureSpec() {
	static const FeatureSpec fallback{"default", false, false};
	return fallback;
}

static const FeatureSpec& ResolveFeatureSpec(const std::string& name) {
	std::string lowered = ToLower(name);
	for (const auto& spec : GetFeatureSpecs()) {
		if (spec.name == lowered) {
			return spec;
		}
	}
	return DefaultFeatureSpec();
}

static std::string JoinFeatureNames() {
	std::string joined;
	for (const auto& spec : GetFeatureSpecs()) {
		if (!joined.empty()) {
			joined += ", ";
		}
		joined += spec.name;
	}
	return joined;
}

static std::string ToLower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

static std::string Trim(std::string value) {
	const char* whitespace = " \t\n\r";
	auto start = value.find_first_not_of(whitespace);
	if (start == std::string::npos) {
		return "";
	}
	auto end = value.find_last_not_of(whitespace);
	return value.substr(start, end - start + 1);
}

static bool ParseBool(const std::string& value, bool defaultValue = false) {
	std::string lowered = ToLower(value);
	if (lowered == "true" || lowered == "1" || lowered == "yes") {
		return true;
	}
	if (lowered == "false" || lowered == "0" || lowered == "no") {
		return false;
	}
	return defaultValue;
}

static long long ParseInt(const std::string& value, long long defaultValue) {
	try {
		size_t idx = 0;
		long long v = std::stoll(value, &idx);
		if (idx != value.size()) {
			return defaultValue;
		}
		return v;
	} catch (...) {
		return defaultValue;
	}
}

static double ParseDouble(const std::string& value, double defaultValue) {
	try {
		size_t idx = 0;
		double v = std::stod(value, &idx);
		if (idx != value.size()) {
			return defaultValue;
		}
		return v;
	} catch (...) {
		return defaultValue;
	}
}

static CpuInfo DetectCpuInfo() {
	CpuInfo info;
	unsigned int logical = std::thread::hardware_concurrency();
	if (logical > 0) {
		info.logical = logical;
	}
#if defined(__APPLE__)
	int physical = 0;
	size_t size = sizeof(physical);
	if (sysctlbyname("hw.physicalcpu", &physical, &size, nullptr, 0) == 0 && physical > 0) {
		info.physical = static_cast<unsigned int>(physical);
	}
#endif
	return info;
}

static int DetectGpuCountFromEnv() {
	const char* explicitEnv = std::getenv("MULTIBLOCK_NUM_GPUS");
	if (explicitEnv) {
		int v = std::atoi(explicitEnv);
		return std::max(0, v);
	}
	const char* cudaVisible = std::getenv("CUDA_VISIBLE_DEVICES");
	if (cudaVisible) {
		std::string s = Trim(cudaVisible);
		if (s.empty() || s == "-1" || ToLower(s) == "nodevfiles") {
			return 0;
		}
		int count = 1;
		for (char c : s) {
			if (c == ',') {
				count++;
			}
		}
		return std::max(0, count);
	}
	return 0;
}

#if defined(__APPLE__)
static int DetectAppleGpuCoresViaSystemProfiler() {
	FILE* pipe = popen("system_profiler SPDisplaysDataType 2>/dev/null", "r");
	if (!pipe) {
		return 0;
	}

	int bestCores = 0;
	char buffer[1024];
	while (fgets(buffer, sizeof(buffer), pipe)) {
		std::string line = Trim(buffer);
		auto pos = line.find("Total Number of Cores:");
		if (pos == std::string::npos) {
			continue;
		}
		std::string value = Trim(line.substr(pos + std::string("Total Number of Cores:").size()));
		int cores = static_cast<int>(ParseInt(value, 0));
		bestCores = std::max(bestCores, cores);
	}

	pclose(pipe);
	return bestCores;
}
#endif

static GpuInfo DetectGpuInfo() {
	GpuInfo info;
	info.count = DetectGpuCountFromEnv();
	if (info.count > 0) {
		info.note = "GPU count inferred from environment";
		return info;
	}

#if defined(__APPLE__)
	// On Apple Silicon, there is typically a single integrated GPU device.
	// We treat it as 1 GPU, and record the reported Metal core count.
	info.cores = DetectAppleGpuCoresViaSystemProfiler();
	if (info.cores > 0) {
		info.count = 1;
		info.name = "Apple Silicon GPU";
		info.note = "GPU detected via system_profiler (Metal cores=" + std::to_string(info.cores) + ")";
		return info;
	}
#endif

	info.note = "No GPU detected (set MULTIBLOCK_NUM_GPUS or CUDA_VISIBLE_DEVICES to override)";
	return info;
}

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

static MpiInfo DetectMpiInfoFromEnv() {
	MpiInfo info;
	const char* sizeEnv = std::getenv("OMPI_COMM_WORLD_SIZE");
	if (!sizeEnv) sizeEnv = std::getenv("PMI_SIZE");
	if (!sizeEnv) sizeEnv = std::getenv("SLURM_NTASKS");
	const char* rankEnv = std::getenv("OMPI_COMM_WORLD_RANK");
	if (!rankEnv) rankEnv = std::getenv("PMI_RANK");
	if (!rankEnv) rankEnv = std::getenv("SLURM_PROCID");
	if (sizeEnv) {
		info.size = std::max(1, std::atoi(sizeEnv));
		info.active = info.size > 1;
	}
	if (rankEnv) {
		info.rank = std::max(0, std::atoi(rankEnv));
	}
	return info;
}

static std::unordered_map<std::string, std::string> ReadConfigFile(const std::string& path) {
	std::unordered_map<std::string, std::string> values;
	std::ifstream in(path);
	if (!in) {
		return values;
	}
	std::string line;
	while (std::getline(in, line)) {
		line = Trim(line);
		if (line.empty()) {
			continue;
		}
		if (line.rfind("#", 0) == 0 || line.rfind("//", 0) == 0) {
			continue;
		}
		auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		std::string key = Trim(line.substr(0, eq));
		std::string value = Trim(line.substr(eq + 1));
		values[ToLower(key)] = value;
	}
	return values;
}

static void ApplyConfigOverrides(SolverConfig& config, const std::unordered_map<std::string, std::string>& values) {
	auto it = values.find("mode");
	if (it != values.end() && !it->second.empty()) {
		config.mode = ToLower(it->second);
	}
	it = values.find("feature");
	if (it != values.end() && !it->second.empty()) {
		config.feature = ToLower(it->second);
		if (config.feature == "alt_hello" || config.feature == "hello" || config.feature == "hello_alt") {
			config.feature = "cpu_accelerated";
		}
	}
	it = values.find("optimized_output");
	if (it != values.end() && !it->second.empty()) {
		config.optimized_output = it->second;
	}
	it = values.find("use_mpi");
	if (it != values.end() && !it->second.empty()) {
		config.use_mpi = ToLower(it->second);
	}

	it = values.find("cpu_threads");
	if (it != values.end() && !it->second.empty()) {
		config.cpu_threads = static_cast<int>(ParseInt(it->second, config.cpu_threads));
	}
	it = values.find("min_cpu_for_gpu");
	if (it != values.end() && !it->second.empty()) {
		config.min_cpu_for_gpu = static_cast<int>(ParseInt(it->second, config.min_cpu_for_gpu));
	}
	it = values.find("num_gpus");
	if (it != values.end() && !it->second.empty()) {
		config.num_gpus = static_cast<int>(ParseInt(it->second, config.num_gpus));
	}
	it = values.find("gpu_work_fraction");
	if (it != values.end() && !it->second.empty()) {
		config.gpu_work_fraction = ParseDouble(it->second, config.gpu_work_fraction);
	}

	it = values.find("work_items");
	if (it != values.end() && !it->second.empty()) {
		config.work_items = static_cast<std::uint64_t>(std::max(1LL, ParseInt(it->second, static_cast<long long>(config.work_items))));
	}
	it = values.find("inner_iters");
	if (it != values.end() && !it->second.empty()) {
		config.inner_iters = static_cast<std::uint32_t>(std::max(1LL, ParseInt(it->second, static_cast<long long>(config.inner_iters))));
	}
	it = values.find("repetitions");
	if (it != values.end() && !it->second.empty()) {
		config.repetitions = static_cast<int>(std::max(1LL, ParseInt(it->second, config.repetitions)));
	}
}

static SolverConfig ParseArgs(int argc, char** argv) {
	SolverConfig config;
	std::vector<std::string> args(argv + 1, argv + argc);
	for (size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--config" && i + 1 < args.size()) {
			config.config_path = args[++i];
			config.solver_file = config.config_path;
		} else if (args[i] == "--mode" && i + 1 < args.size()) {
			config.mode = ToLower(args[++i]);
		} else if (args[i] == "--feature" && i + 1 < args.size()) {
			config.feature = ToLower(args[++i]);
		} else if (args[i] == "--use-mpi" && i + 1 < args.size()) {
			config.use_mpi = ToLower(args[++i]);
		} else if (args[i] == "--cpu-threads" && i + 1 < args.size()) {
			config.cpu_threads = static_cast<int>(ParseInt(args[++i], config.cpu_threads));
		} else if (args[i] == "--gpus" && i + 1 < args.size()) {
			config.num_gpus = static_cast<int>(ParseInt(args[++i], config.num_gpus));
		} else if (args[i] == "--min-cpu-for-gpu" && i + 1 < args.size()) {
			config.min_cpu_for_gpu = static_cast<int>(ParseInt(args[++i], config.min_cpu_for_gpu));
		} else if (args[i] == "--gpu-work-fraction" && i + 1 < args.size()) {
			config.gpu_work_fraction = ParseDouble(args[++i], config.gpu_work_fraction);
		} else if (args[i] == "--work-items" && i + 1 < args.size()) {
			config.work_items = static_cast<std::uint64_t>(std::max(1LL, ParseInt(args[++i], static_cast<long long>(config.work_items))));
		} else if (args[i] == "--inner-iters" && i + 1 < args.size()) {
			config.inner_iters = static_cast<std::uint32_t>(std::max(1LL, ParseInt(args[++i], static_cast<long long>(config.inner_iters))));
		} else if (args[i] == "--repetitions" && i + 1 < args.size()) {
			config.repetitions = static_cast<int>(std::max(1LL, ParseInt(args[++i], config.repetitions)));
		} else if (args[i] == "--optimized-output" && i + 1 < args.size()) {
			config.optimized_output = args[++i];
		} else if (args[i].rfind("--", 0) != 0 && config.solver_file == "solver_input.txt") {
			config.config_path = args[i];
			config.solver_file = args[i];
		}
	}
	return config;
}

static bool ShouldUseMpi(const SolverConfig& config, const MpiInfo& mpi) {
	std::string value = ToLower(config.use_mpi);
	if (value == "true" || value == "1" || value == "yes") {
		return true;
	}
	if (value == "false" || value == "0" || value == "no") {
		return false;
	}
	return mpi.active;
}

static void WriteSolverFile(const std::string& path, const SolverConfig& config, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi, const std::string& title) {
	std::ofstream out(path);
	if (!out) {
		return;
	}

	auto suggestedCpuThreads = SuggestCounts(cpu.logical);
	auto suggestedMpiRanks = SuggestCounts(cpu.logical);

	out << "# " << title << "\n";
	out << "# Summary: minimal, user-facing configuration\n";
	out << "configuration=cpu_logical=" << cpu.logical;
	if (cpu.physical > 0) {
		out << ", cpu_physical=" << cpu.physical;
	}
	out << ", gpus=" << gpu.count;
	if (gpu.cores > 0) {
		out << ", gpu_cores=" << gpu.cores;
	}
	out << "\n\n";

	out << "mode=" << config.mode << "\n";
	out << "feature=" << config.feature << "\n";
	out << "use_mpi=" << config.use_mpi << "\n";
	out << "optimized_output=" << config.optimized_output << "\n\n";

	out << "work_items=" << config.work_items << "\n";
	out << "inner_iters=" << config.inner_iters << "\n";
	out << "repetitions=" << config.repetitions << "\n\n";

	out << "cpu_threads=" << config.cpu_threads << "\n";
	out << "min_cpu_for_gpu=" << config.min_cpu_for_gpu << "\n";
	out << "num_gpus=" << config.num_gpus << "\n";
	out << "gpu_work_fraction=" << std::fixed << std::setprecision(3) << config.gpu_work_fraction << "\n\n";

	out << "# Examples\n";
	out << "# 1) Preprocess (auto-benchmark & write optimized file)\n";
	out << "#    mode=preprocess\n";
	out << "# 2) CPU-only accelerated run\n";
	out << "#    feature=cpu_accelerated\n";
	out << "#    cpu_threads=" << std::min<unsigned int>(cpu.logical, 8) << "\n";
	out << "# 3) GPU-only run (if available)\n";
	out << "#    feature=gpu_accelerated\n";
	out << "#    num_gpus=1\n";
}

static std::uint64_t SplitWorkBegin(std::uint64_t total, int partIndex, int partCount) {
	std::uint64_t base = total / static_cast<std::uint64_t>(partCount);
	std::uint64_t rem = total % static_cast<std::uint64_t>(partCount);
	std::uint64_t begin = static_cast<std::uint64_t>(partIndex) * base + std::min<std::uint64_t>(static_cast<std::uint64_t>(partIndex), rem);
	return begin;
}

static std::uint64_t SplitWorkEnd(std::uint64_t total, int partIndex, int partCount) {
	std::uint64_t begin = SplitWorkBegin(total, partIndex, partCount);
	std::uint64_t base = total / static_cast<std::uint64_t>(partCount);
	std::uint64_t rem = total % static_cast<std::uint64_t>(partCount);
	std::uint64_t size = base + (static_cast<std::uint64_t>(partIndex) < rem ? 1 : 0);
	return begin + size;
}

static double ComputeKernel(std::uint64_t seed, std::uint32_t innerIters) {
	double x = 0.123456789 + (static_cast<double>(seed % 1024) * 0.0009765625);
	double y = 0.987654321 - (static_cast<double>((seed / 1024) % 1024) * 0.00048828125);
	for (std::uint32_t i = 0; i < innerIters; ++i) {
		x = x * 1.0000001192092896 + y * 0.9999999403953552 + 0.0000003;
		y = y * 1.0000002384185791 - x * 0.9999998211860657 + 0.0000001;
		if (x > 1e6) x *= 1e-6;
		if (y < -1e6) y *= 1e-6;
	}
	return x + y;
}

struct BenchmarkResult {
	double seconds = 0.0;
	double checksum = 0.0;
	std::uint64_t work_items = 0;
	std::uint32_t inner_iters = 0;
	int cpu_threads = 1;
	int gpu_workers = 0;
	std::string feature;
};

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

	// CPU workers
	for (int w = 0; w < cpuWorkerCount; ++w) {
		std::uint64_t begin = SplitWorkBegin(cpuItems, w, cpuWorkerCount);
		std::uint64_t end = SplitWorkEnd(cpuItems, w, cpuWorkerCount);
		workers.emplace_back(runRange, w, rankBegin + begin, rankBegin + end,
			static_cast<std::uint64_t>(mpi.rank) * 1'000'000'000ULL);
	}

	// "GPU" workers (currently a CPU fallback implementation)
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

static BenchmarkResult RunBenchmark(const SolverConfig& config, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi) {
	BenchmarkResult best;
	best.seconds = std::numeric_limits<double>::infinity();
	for (int r = 0; r < config.repetitions; ++r) {
		auto res = RunBenchmarkOnce(config, cpu, gpu, mpi);
		// take best time (more stable than mean for quick tests)
		if (res.seconds < best.seconds) {
			best = res;
		}
	}
	return best;
}

static double ThroughputItemsPerSecond(const BenchmarkResult& r) {
	if (r.seconds <= 0.0) {
		return 0.0;
	}
	return static_cast<double>(r.work_items) / r.seconds;
}

static void PrintBenchmarkResult(const BenchmarkResult& r, const MpiInfo& mpi) {
	std::cout << "[rank " << mpi.rank << "/" << mpi.size << "] "
			  << "feature=" << r.feature
			  << " cpu_threads=" << r.cpu_threads
			  << " gpus=" << r.gpu_workers
			  << " time_s=" << std::fixed << std::setprecision(6) << r.seconds
			  << " items_per_s=" << std::fixed << std::setprecision(2) << ThroughputItemsPerSecond(r)
			  << " checksum=" << std::setprecision(6) << r.checksum
			  << std::endl;
}

static void WritePreprocessReportHeader(std::ofstream& out) {
	out << "feature,cpu_threads,gpus,work_items,inner_iters,repetitions,best_time_s,items_per_s,checksum\n";
}

static void WritePreprocessReportRow(std::ofstream& out, const BenchmarkResult& r, int reps) {
	out << r.feature << ','
		<< r.cpu_threads << ','
		<< r.gpu_workers << ','
		<< r.work_items << ','
		<< r.inner_iters << ','
		<< reps << ','
		<< std::fixed << std::setprecision(9) << r.seconds << ','
		<< std::fixed << std::setprecision(3) << ThroughputItemsPerSecond(r) << ','
		<< std::setprecision(9) << r.checksum << '\n';
}

static SolverConfig MakeCandidateConfig(const SolverConfig& base, const std::string& feature, int cpuThreads, int gpus) {
	SolverConfig cfg = base;
	cfg.mode = "run";
	cfg.feature = feature;
	cfg.cpu_threads = cpuThreads;
	cfg.num_gpus = gpus;
	return cfg;
}

static SolverConfig RunPreprocess(const SolverConfig& base, const CpuInfo& cpu, const GpuInfo& gpu, const MpiInfo& mpi) {
	// For now: preprocess is rank0-only to avoid cross-rank coordination without MPI collectives.
	// If you run under mpirun, each rank will benchmark independently and rank0 will write the optimized file.
	if (mpi.rank != 0) {
		SolverConfig dummy = base;
		dummy.mode = "run";
		return dummy;
	}

	std::ofstream report("preprocess_report.csv");
	if (report) {
		WritePreprocessReportHeader(report);
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

	BenchmarkResult best;
	best.seconds = std::numeric_limits<double>::infinity();
	SolverConfig bestCfg = base;
	bestCfg.mode = "run";
	std::vector<BenchmarkResult> allResults;

	auto consider = [&](const SolverConfig& candidate) {
		auto res = RunBenchmark(candidate, cpu, gpu, MpiInfo{0, 1, false});
		allResults.push_back(res);
		if (report) {
			WritePreprocessReportRow(report, res, candidate.repetitions);
		}
		if (res.seconds < best.seconds) {
			best = res;
			bestCfg = candidate;
		}
	};

	for (const auto& spec : GetFeatureSpecs()) {
		if (!spec.uses_cpu && !spec.uses_gpu) {
			// default baseline
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
		// CPU-only accelerated
		for (int t : cpuCandidates) {
			consider(MakeCandidateConfig(base, spec.name, t, 0));
		}
	}

	std::cout << "Preprocess best config: feature=" << bestCfg.feature
			  << " cpu_threads=" << bestCfg.cpu_threads
			  << " num_gpus=" << bestCfg.num_gpus
			  << " best_time_s=" << std::fixed << std::setprecision(6) << best.seconds
			  << " items_per_s=" << std::fixed << std::setprecision(2) << ThroughputItemsPerSecond(best)
			  << std::endl;

	SolverConfig outCfg = bestCfg;
	outCfg.mode = "run";
	WriteSolverFile(base.optimized_output, outCfg, cpu, gpu, mpi, "Optimized solver configuration (generated by multiblock preprocess)");
	{
		std::ofstream tableOut(base.optimized_output, std::ios::app);
		if (tableOut) {
			tableOut << "\n# Preprocess summary\n";
			tableOut << "# Best option: feature=" << bestCfg.feature
					 << ", cpu_threads=" << bestCfg.cpu_threads
					 << ", num_gpus=" << bestCfg.num_gpus
					 << ", best_time_s=" << std::fixed << std::setprecision(6) << best.seconds
					 << ", items_per_s=" << std::fixed << std::setprecision(2) << ThroughputItemsPerSecond(best)
					 << "\n";
			tableOut << "# Suggested options (edit feature/cpu_threads/num_gpus above to select)\n";
			tableOut << "# feature,cpu_threads,num_gpus,best_time_s,items_per_s\n";
			for (const auto& entry : allResults) {
				tableOut << "# " << entry.feature << ','
						  << entry.cpu_threads << ','
						  << entry.gpu_workers << ','
						  << std::fixed << std::setprecision(6) << entry.seconds << ','
						  << std::fixed << std::setprecision(2) << ThroughputItemsPerSecond(entry) << "\n";
			}
			tableOut << "\n# Example: apply the best option\n";
			tableOut << "# feature=" << bestCfg.feature << "\n";
			tableOut << "# cpu_threads=" << bestCfg.cpu_threads << "\n";
			tableOut << "# num_gpus=" << bestCfg.num_gpus << "\n";
		}
	}
	std::cout << "Wrote optimized solver file: " << base.optimized_output << std::endl;
	if (report) {
		std::cout << "Wrote preprocess report: preprocess_report.csv" << std::endl;
	}

	return outCfg;
}

} // namespace

int main(int argc, char** argv) {
	SolverConfig config = ParseArgs(argc, argv);
	CpuInfo cpu = DetectCpuInfo();
	GpuInfo gpu = DetectGpuInfo();
	MpiInfo mpiEnv = DetectMpiInfoFromEnv();
	bool useMpi = ShouldUseMpi(config, mpiEnv);
	MpiInfo mpi = useMpi ? mpiEnv : MpiInfo{};

	std::ifstream existing(config.config_path);
	if (!existing.good()) {
		if (mpi.rank == 0) {
			WriteSolverFile(config.config_path, config, cpu, gpu, mpi, "Solver configuration file (generated by multiblock)");
			std::cout << "Generated solver file: " << config.config_path << std::endl;
		}
	} else {
		auto values = ReadConfigFile(config.config_path);
		ApplyConfigOverrides(config, values);
	}

	if (ToLower(config.mode) == "preprocess") {
		(void)RunPreprocess(config, cpu, gpu, mpi);
		return 0;
	}

	if (mpi.rank == 0) {
		if (useMpi && mpi.active) {
			std::cout << "MPI runtime detected (env): size=" << mpi.size << std::endl;
		}
		std::cout << "Detected CPU logical_cores=" << cpu.logical;
		if (cpu.physical > 0) {
			std::cout << " physical_cores=" << cpu.physical;
		}
		std::cout << std::endl;
		std::cout << "Detected GPU devices=" << gpu.count;
		if (gpu.cores > 0) {
			std::cout << " gpu_cores=" << gpu.cores;
		}
		if (!gpu.name.empty()) {
			std::cout << " name=\"" << gpu.name << "\"";
		}
		std::cout << std::endl;
		const auto& featureSpec = ResolveFeatureSpec(config.feature);
		if (featureSpec.uses_gpu && gpu.count == 0) {
			std::cout << "Note: GPU feature requested but no GPU detected. "
					  << "This build uses a CPU fallback for GPU workers." << std::endl;
		}
	}

	auto res = RunBenchmark(config, cpu, gpu, mpi);
	PrintBenchmarkResult(res, mpi);
	return 0;
}
