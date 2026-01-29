#include "fastvessels/env_info.hpp"

#include "fastvessels/common.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace fastvessels {

CpuInfo DetectCpuInfo() {
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

GpuInfo DetectGpuInfo() {
	GpuInfo info;
	info.count = DetectGpuCountFromEnv();
	if (info.count > 0) {
		info.note = "GPU count inferred from environment";
		return info;
	}

#if defined(__APPLE__)
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

MpiInfo DetectMpiInfoFromEnv() {
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

} // namespace fastvessels
