# Building the Project

Rift uses CMake as its build system. This guide covers building for Windows.

## Build Process Overview

\htmlonly
<pre class="mermaid">
flowchart LR
    classDef tool fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef output fill:#134e3a,stroke:#10b981,color:#e2e8f0

    Source["Source Code"]:::tool
    CMake["CMake"]:::tool
    Compiler["Compiler"]:::tool
    Shaders["Shader Compiler"]:::tool
    Exe["rift.exe"]:::output
    Assets["Assets"]:::output

    Source --> CMake
    CMake --> Compiler
    Compiler --> Exe
    Shaders --> Exe
    Assets --> Exe
</pre>
\endhtmlonly

## Auto-Detected Features

Both rendering backends are always built into the binary:
- **OpenGL** - Always included (required)
- **Vulkan** - Always included (required); CMake fails configuration if the Vulkan SDK is missing
- **FreeType** - Included if FreeType library is found (optional; text rendering disabled when missing)

The graphics backend is chosen at runtime, not at build time: `startupRenderer` in
`rift.project.json` picks the boot backend, and `renderer.set opengl|vulkan` in the developer
console (`F12`) swaps it live.

## Windows

### Quick Build (Batch Script)

Set `VCPKG_ROOT` to your vcpkg checkout first. `build.bat` configures through
`cmake --preset default`, whose preset chain is conditional on `VCPKG_ROOT` being non-empty; with
the variable unset the preset is disabled and the configure step fails immediately. The
dependencies declared in `vcpkg.json` (glm, glfw3, freetype, gtest) are then installed by vcpkg
manifest mode during configure.

We provide a batch script for one-click building:

```cmd
:: Full pipeline
.\build.bat

:: Same pipeline without the blocking static-analysis step
.\build.bat --skip-tidy

:: Usage
.\build.bat --help
```

The script runs five steps in order and stops at the first failure:

1. **clang-format** - rewrites every `src/*.cpp|hpp|h|c` file in place. This edits your working
   tree; commit or stash first if you want to see the formatting as a separate change.
2. **CMake configure** - `cmake --preset default` (Visual Studio 17 2022 generator, vcpkg manifest
   install into `build/`).
3. **clang-tidy** - static analysis over every `src/*.cpp` and `tests/*.cpp`, one file at a time,
   against a `compile_commands.json` produced by the `build-cdb` Ninja sidecar. This step is
   blocking: any diagnostic aborts the pipeline. Skip it with `--skip-tidy`.
4. **Build** - Debug then Release of target `rift`. Shader-to-SPIR-V compilation, the asset and
   shader copy, the `rift.project.json` copy and the `rift.save*.json` copy all run as part of this
   step, driven by CMake rather than by the script.
5. **Doxygen** - HTML documentation, if Doxygen is installed.

Steps 1 and 3 are skipped with a notice when `clang-format` or `clang-tidy` is not on `PATH`.

### Manual Build (Command Line)

For more control over the build process:

```cmd
:: Create build directory
mkdir build
cd build

:: Configure CMake
cmake ..

:: Build (choose Debug or Release)
cmake --build . --config Release

:: Run
.\Release\rift.exe
```

### Visual Studio

1. Open Visual Studio
2. Select **File > Open > CMake...**
3. Navigate to the project root and select `CMakeLists.txt`
4. Visual Studio will configure automatically
5. Select build configuration from toolbar
6. Press **F5** to build and run

## Shader Compilation

### OpenGL

OpenGL shaders (GLSL) are loaded at runtime from the `shaders/` directory. No pre-compilation needed.

### Vulkan

Vulkan requires shaders to be pre-compiled to SPIR-V format:

\htmlonly
<pre class="mermaid">
flowchart LR
    classDef source fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef tool fill:#f39c12,stroke:#e67e22,color:#1a1a2e
    classDef output fill:#134e3a,stroke:#10b981,color:#e2e8f0

    GLSL["Geometry.vert/frag"]:::source
    Compiler["glslangValidator"]:::tool
    SPIRV["Geometry.vert.spv"]:::output

    GLSL --> Compiler --> SPIRV
</pre>
\endhtmlonly

The `build.bat` script compiles shaders via `glslangValidator` (installed with the Vulkan SDK, which is required).

**Manual compilation** - these are the four the build itself runs. `-DUSE_VULKAN` is part of the
command; SPIR-V compiled without it will not match the build's:

```cmd
glslangValidator -V -DUSE_VULKAN shaders/Geometry.vert -o shaders/Geometry.vert.spv
glslangValidator -V -DUSE_VULKAN shaders/Geometry.frag -o shaders/Geometry.frag.spv
glslangValidator -V -DUSE_VULKAN shaders/Geometry3D.vert -o shaders/Geometry3D.vert.spv
glslangValidator -V -DUSE_VULKAN shaders/Geometry3D.frag -o shaders/Geometry3D.frag.spv
```

`Geometry.*` is the screen-space 2D pair; `Geometry3D.*` is the world-space MVP path.

Ensure `glslangValidator` is in your PATH (installed with Vulkan SDK).

`.spv` files are build artifacts and are gitignored on purpose. A stale one keeps an old Vulkan
shader alive with no error: delete it when the Vulkan output disagrees with the GLSL source.

## Build Configurations

With Visual Studio generators on Windows, the build configuration is selected at build time using `--config`:

### Debug

- Compiler optimizations disabled
- Debug symbols included
- Assertions enabled
- Slower runtime performance

```cmd
cmake --build . --config Debug
```

### Release

- Full compiler optimizations
- No debug symbols
- Assertions disabled
- Best runtime performance

```cmd
cmake --build . --config Release
```

## Tests

`BUILD_TESTS` defaults to `ON`, and the `rift_tests` target is configured into the same `build/`
tree as the game. A plain `cmake --build build --config Release` therefore builds the tests too,
and a test compile error fails a build you may think is game-only. Add `--target rift` to restrict
a build to the game.

```cmd
:: Configure, build rift_tests, run every test
.\test.bat

:: Or drive the pieces directly
cmake --build build --config Release --target rift_tests
ctest --test-dir build -C Release --output-on-failure
```

## Build Output Structure

After a successful build:

```
build/
|-- Debug/                  (or Release/)
|   |-- rift.exe            # Main executable
|   |-- rift_tests.exe      # Test binary (BUILD_TESTS=ON)
|   |-- rift.project.json   # Manifest, copied from the repo root
|   |-- rift.save*.json     # Save/map files, copied from the repo root if present
|   |-- assets/             # Copied verbatim from assets/; its layout is whatever
|   |                       # the manifest paths point at
|   +-- shaders/            # GLSL sources plus their compiled .spv siblings
+-- ...
```

`assets/` is not part of the repository, so the copy step produces nothing when it is absent.

## Troubleshooting

### Dependency Errors

If CMake reports missing dependencies (GLFW, GLM, GLAD, stb_image, nlohmann/json, ecs), see the [Setup Guide - Troubleshooting](SETUP.md#troubleshooting) section for solutions. All six are hard `FATAL_ERROR` checks; `setup.ps1` provisions them.

### Vulkan Errors

| Error                              | Solution                                 |
|------------------------------------|------------------------------------------|
| "Failed to create Vulkan instance" | Update GPU drivers, reinstall Vulkan SDK |
| "No suitable GPU found"            | Ensure GPU supports Vulkan 1.0 and has current drivers |
| "Shader compilation failed"        | Run shader compilation scripts           |

### Runtime Errors

| Error                | Solution                                                      |
|----------------------|---------------------------------------------------------------|
| "Assets not found"   | Copy `assets/` folder and check paths in `rift.project.json`  |
| "Shader load failed" | Copy `shaders/` folder to executable directory                |
| "Font not found"     | Ensure FreeType is installed and fonts are in `assets/fonts/` |

### Missing Assets

If the build process doesn't copy assets automatically:

```cmd
:: From build directory
xcopy /E /Y ..\assets .\Release\assets\
xcopy /E /Y ..\shaders .\Release\shaders\
copy ..\rift.project.json .\Release\rift.project.json
```

## See Also

- [Setup Guide](SETUP.md) - Installing dependencies
- [Editor Guide](EDITOR.md) - Using the level editor after building
- [Project Manifest](PROJECT_MANIFEST.md) - Configuring startup assets
