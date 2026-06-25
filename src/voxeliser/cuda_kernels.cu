#include "voxeliser/cuda_kernels.hpp"

#ifdef MULTIBLOCK_ENABLE_CUDA

#include <cuda_runtime.h>

namespace voxeliser {

__global__ void SurfaceVoxelKernelStub() {}

void LaunchSurfaceVoxelKernel() {
	SurfaceVoxelKernelStub<<<1, 1>>>();
	cudaDeviceSynchronize();
}

} // namespace voxeliser

#endif
