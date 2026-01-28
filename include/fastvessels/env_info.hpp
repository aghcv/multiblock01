#pragma once

#include <string>

namespace fastvessels {

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

CpuInfo DetectCpuInfo();
GpuInfo DetectGpuInfo();
MpiInfo DetectMpiInfoFromEnv();

} // namespace fastvessels
