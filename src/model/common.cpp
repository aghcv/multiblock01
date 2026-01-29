#include "fastvessels/common.hpp"

#include <algorithm>
#include <cctype>

namespace fastvessels {

std::string ToLower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::string Trim(std::string value) {
	const char* whitespace = " \t\n\r";
	auto start = value.find_first_not_of(whitespace);
	if (start == std::string::npos) {
		return "";
	}
	auto end = value.find_last_not_of(whitespace);
	return value.substr(start, end - start + 1);
}

bool ParseBool(const std::string& value, bool defaultValue) {
	std::string lowered = ToLower(value);
	if (lowered == "true" || lowered == "1" || lowered == "yes") {
		return true;
	}
	if (lowered == "false" || lowered == "0" || lowered == "no") {
		return false;
	}
	return defaultValue;
}

long long ParseInt(const std::string& value, long long defaultValue) {
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

double ParseDouble(const std::string& value, double defaultValue) {
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

std::uint64_t SplitWorkBegin(std::uint64_t total, int partIndex, int partCount) {
	std::uint64_t base = total / static_cast<std::uint64_t>(partCount);
	std::uint64_t rem = total % static_cast<std::uint64_t>(partCount);
	std::uint64_t begin = static_cast<std::uint64_t>(partIndex) * base + std::min<std::uint64_t>(static_cast<std::uint64_t>(partIndex), rem);
	return begin;
}

std::uint64_t SplitWorkEnd(std::uint64_t total, int partIndex, int partCount) {
	std::uint64_t begin = SplitWorkBegin(total, partIndex, partCount);
	std::uint64_t base = total / static_cast<std::uint64_t>(partCount);
	std::uint64_t rem = total % static_cast<std::uint64_t>(partCount);
	std::uint64_t size = base + (static_cast<std::uint64_t>(partIndex) < rem ? 1 : 0);
	return begin + size;
}

double ComputeKernel(std::uint64_t seed, std::uint32_t innerIters) {
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

} // namespace fastvessels
