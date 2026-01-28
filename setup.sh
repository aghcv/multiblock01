#!/usr/bin/env bash
set -euo pipefail

# Minimal setup for the 'light' branch
# - Creates conda env with cmake, ninja, vtk
# - Configures and builds the project with Ninja

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_NAME="cxxgeom"

say() { echo "[setup] $*"; }

# 1) Conda channel configuration
if command -v conda >/dev/null 2>&1; then
  say "Configuring conda-forge channel (strict priority)"
  conda config --add channels conda-forge || true
  conda config --set channel_priority strict || true
else
  say "Conda not found. Please install Miniforge/Conda and re-run."
  exit 1
fi

# 2) Create or update environment
say "Creating environment '${ENV_NAME}' from environment.yml"
conda env remove -n "${ENV_NAME}" --yes || true
conda env create -f "${PROJECT_DIR}/environment.yml" -n "${ENV_NAME}"

# 3) Activate and build
# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate "${ENV_NAME}"

say "Configuring CMake (Release)"
rm -rf "${PROJECT_DIR}/build"
cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build" -G Ninja -DCMAKE_BUILD_TYPE=Release

say "Building with Ninja"
cmake --build "${PROJECT_DIR}/build" --config Release

say "Done. Binary is at: ${PROJECT_DIR}/build/multiblock.mbx"
