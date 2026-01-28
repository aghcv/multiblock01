# multiblock

Note: You are on the 'light' branch. This is a minimal VTK-only setup.

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
Run the setup script:
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

`multiblock` exports a `compile_commands.json` file for IDE integration.

## 1️⃣ CMake configuration
In the top-level `CMakeLists.txt`, make sure this line is present:
```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

This generates `build/compile_commands.json` after configuration.

## 2️⃣ Build
To rebuild manually:
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
