# multiblock

Minimal VTK-based tool that converts a VTP surface into a VTM multiblock file.

## Build
```bash
bash setup.sh
```

## Run
```bash
./build/multiblock.mbx -in raw/vtp/cardiovascular.vtp -out output/geometry_multiblock.vtm
```

## Required inputs
- `-in`: path to a VTP surface file
- `-out`: output VTM path (optional, default: output/geometry_multiblock.vtm)

## Test
```bash
ctest --test-dir build --output-on-failure
```

## Current tests
- `vtp_to_vtm`: converts the sample VTP, writes a VTM, and checks it loads with blocks
