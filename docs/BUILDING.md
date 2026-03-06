# Building the Project

The Rift Game Engine uses CMake as its build system. This guide covers building for Windows.

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

The build automatically detects and includes all available features:
- **OpenGL** - Always included (required)
- **Vulkan** - Included if Vulkan SDK is installed
- **FreeType** - Included if FreeType library is found

The graphics backend (OpenGL/Vulkan) is selected at runtime, not build time.

## Windows

### Quick Build (Batch Script)

We provide a batch script for one-click building:

```cmd
.\build.bat
```

This script automatically:
1. Builds both Debug and Release configurations
2. Compiles shaders to SPIR-V (if Vulkan SDK is installed)
3. Copies assets to build directories
4. Copies save files from project root to build directories
5. Generates Doxygen documentation (if Doxygen is installed)

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

    GLSL["sprite.vert/frag"]:::source
    Compiler["glslangValidator"]:::tool
    SPIRV["sprite.vert.spv"]:::output

    GLSL --> Compiler --> SPIRV
</pre>
\endhtmlonly

The `build.bat` script automatically compiles shaders if `glslangValidator` is in your PATH (installed with Vulkan SDK).

**Manual compilation:**
```cmd
glslangValidator -V shaders/sprite.vert -o shaders/sprite.vert.spv
glslangValidator -V shaders/sprite.frag -o shaders/sprite.frag.spv
glslangValidator -V shaders/text.vert -o shaders/text.vert.spv
glslangValidator -V shaders/text.frag -o shaders/text.frag.spv
```

Ensure `glslangValidator` is in your PATH (installed with Vulkan SDK).

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

## Build Output Structure

After a successful build:

```
build/
|-- Debug/            (or Release/)
|   |-- rift.exe      # Main executable
|   |-- assets/       # Game assets (copied)
|   |   |-- sprites/
|   |   |-- fonts/
|   |   +-- maps/
|   +-- shaders/      # Shader files (copied)
+-- ...
```

## Troubleshooting

### Dependency Errors

If CMake reports missing dependencies (GLFW, GLM, GLAD, stb_image), see the [Setup Guide - Troubleshooting](SETUP.md#troubleshooting) section for solutions.

### Vulkan Errors

| Error                              | Solution                                 |
|------------------------------------|------------------------------------------|
| "Failed to create Vulkan instance" | Update GPU drivers, reinstall Vulkan SDK |
| "No suitable GPU found"            | Ensure GPU supports Vulkan 1.2+          |
| "Shader compilation failed"        | Run shader compilation scripts           |

### Runtime Errors

| Error                | Solution                                                      |
|----------------------|---------------------------------------------------------------|
| "Assets not found"   | Copy `assets/` folder to executable directory                 |
| "Shader load failed" | Copy `shaders/` folder to executable directory                |
| "Font not found"     | Ensure FreeType is installed and fonts are in `assets/fonts/` |

### Missing Assets

If the build process doesn't copy assets automatically:

```cmd
:: From build directory
xcopy /E /Y ..\assets .\Release\assets\
xcopy /E /Y ..\shaders .\Release\shaders\
```

## See Also

- [Setup Guide](SETUP.md) - Installing dependencies
- [Editor Guide](EDITOR.md) - Using the level editor after building
