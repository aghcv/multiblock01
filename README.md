# multiblock

Note: You are on the 'light' branch — a minimal, VTK-only setup intended for a fast, no-frills build. OpenVDB/VMTK, unit tests, and CI are removed here; see the main branch for full functionality.

`multiblock` provides tools to analyze and manipulate surface meshes using VTK multiblock structures.

```bash
multiblock/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                  # example app
│   ├── io_utils.cpp
│   └── ...
├── include/
│   ├── fastvessels/
│   │   ├── io_utils.hpp
│   │   └── ...
├── environment.yml
├── setup.sh
└── README.md
```

---

# 🧩 Quickstart: minimal VTK-only setup

## 1. Clone the repository
```bash
git clone https://github.com/<your-org>/<your-repo>.git
cd <your-repo>
```

## 2. One-step setup
Run the setup script; it will create the env, configure and build:
```bash
bash setup.sh
```

## 3. Run example
```bash
./build/multiblock
```
You should see: "Hello from multiblock (VTK-only, light branch)".

---

# 🧠 Developer Setup (optional)

To enable smooth development and header auto-completion across all IDEs (VS Code, CLion, Qt Creator, Visual Studio, etc.),  
`multiblock` exports a `compile_commands.json` file that describes all compiler flags and include paths.

## 1️⃣ CMake configuration
In the top-level `CMakeLists.txt`, make sure this line is present:
```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

This automatically generates a `build/compile_commands.json` file after configuration.

## 2️⃣ Build
The setup script already builds. To rebuild manually:
```bash
mkdir -p build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

## 3️⃣ IDE support
Most IDEs can detect `compile_commands.json` automatically.  
If yours doesn’t, open your IDE’s **CMake** or **Project Settings** and point it to:

```
/path/to/multiblock/build/compile_commands.json
```

## 4️⃣ Optional (VS Code only)
If you use VS Code, create a file `.vscode/settings.json` at the repository root:

```json
{
    "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json"
}
```

Reload the VS Code window (`Cmd ⇧ P → Developer: Reload Window`) and all VTK headers should resolve automatically 🎯
