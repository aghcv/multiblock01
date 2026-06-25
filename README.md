# multiblock

Minimal VTK-based tool that converts a VTP surface into a VTM multiblock file.

## Build
```bash
bash setup.sh
```

Voxeliser dependencies (OpenVDB/NanoVDB/CUDA) are enabled by default in the
setup script. If you configure manually, use:
```bash
cmake -S . -B build -G Ninja -DENABLE_VDB=ON -DENABLE_CUDA=ON
```

## Run
```bash
./build/multiblock.mbx -in raw/vtp/cardiovascular.vtp -out output/geometry_multiblock.vtm
```

Voxeliser (optional):
```bash
./build/multiblock.mbx -in raw/vtp/cardiovascular.vtp --voxelise \
	--voxel-size 0.5 --export-vdb output/geometry.vdb --export-nanovdb output/geometry.nvdb
```

## Required inputs
- `-in`: path to a VTP surface file
- `-out`: output VTM path (optional, default: output/geometry_multiblock.vtm)

## Optional voxeliser flags
- `--voxelise`: run OpenVDB voxelisation on the input geometry
- `--voxel-size <float>`: voxel size (default: 0.5)
- `--export-vdb <path>`: write OpenVDB grids to file
- `--export-nanovdb <path>`: write NanoVDB grids to file (requires NanoVDB). Multiple grids are written with suffixes like `_sdf.nvdb`.

## Voxeliser dependencies
- OpenVDB (required for voxeliser)
- NanoVDB (optional, for NanoVDB export and GPU upload)
- CUDA toolkit (optional, for GPU kernels)

Example (conda):
```bash
conda install -c conda-forge openvdb
```

Example (brew):
```bash
brew install openvdb
```

## Test
```bash
ctest --test-dir build --output-on-failure
```

## Current tests
- `vtp_to_vtm`: converts the sample VTP, writes a VTM, and checks it loads with blocks
