#pragma once

#include <cstdint>
#include <string>

namespace fastvessels {

std::string ToLower(std::string value);
std::string Trim(std::string value);

bool ParseBool(const std::string& value, bool defaultValue = false);
long long ParseInt(const std::string& value, long long defaultValue);
double ParseDouble(const std::string& value, double defaultValue);

std::uint64_t SplitWorkBegin(std::uint64_t total, int partIndex, int partCount);
std::uint64_t SplitWorkEnd(std::uint64_t total, int partIndex, int partCount);

double ComputeKernel(std::uint64_t seed, std::uint32_t innerIters);

} // namespace fastvessels
