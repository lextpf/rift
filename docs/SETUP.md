# Setting Up the Development Environment

This guide covers installing dependencies and configuring your environment for developing with the Wild Game Engine.

## Prerequisites Overview

\htmlonly
<pre class="mermaid">
graph LR
    classDef required fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef optional fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    subgraph Required["Required Tools"]
        CMake["CMake 3.10+"]:::required
        Git["Git"]:::required
        Compiler["C++23 Compiler"]:::required
    end

    subgraph Libraries["Required Libraries"]
        GLFW["GLFW 3.3+"]:::required
        GLM["GLM"]:::required
        GLAD["GLAD"]:::required
        STB["stb_image"]:::required
    end

    subgraph Optional["Optional"]
        Vulkan["Vulkan SDK"]:::optional
        FreeType["FreeType 2"]:::optional
    end
</pre>
\endhtmlonly

## Required Tools

### CMake

**Version:** 3.10 or higher

| Platform      | Installation                                                               |
|---------------|----------------------------------------------------------------------------|
| Windows       | [Download installer](https://cmake.org/download/) or `choco install cmake` |

Verify installation:
```cmd
cmake --version
```

### C++ Compiler

The project requires C++23 support.

| Platform | Compiler            | Notes                                           |
|----------|---------------------|-------------------------------------------------|
| Windows  | Visual Studio 2022+ | Install "Desktop development with C++" workload |

## Dependencies

### Dependency Overview

| Library    | Purpose                         | Required         |
|------------|---------------------------------|------------------|
| GLFW       | Window creation, input handling | Yes              |
| GLM        | Mathematics (vectors, matrices) | Yes              |
| GLAD       | OpenGL function loading         | Yes (for OpenGL) |
| stb_image  | Image file loading              | Yes              |
| FreeType   | Font rendering                  | Optional         |
| Vulkan SDK | Vulkan graphics API             | Optional         |

### Automatic Setup (Windows)

We provide a PowerShell script to automatically download and configure dependencies:

```powershell
# Open PowerShell in project root
.\setup.ps1
```

This script will:
1. Clone GLFW into `external/glfw/`
2. Download stb_image.h into `external/stb/`
3. Check for GLM and GLAD (provides instructions if missing)

### Manual Setup

If the automatic script doesn't work, follow these steps manually:

#### Project Structure

After setup, your `external/` directory should look like:

```
external/
|-- glfw/
|   |-- CMakeLists.txt
|   |-- include/
|   +-- src/
|-- glad/
|   |-- include/
|   |   |-- glad/
|   |   |   +-- glad.h
|   |   +-- KHR/
|   |       +-- khrplatform.h
|   +-- src/
|       +-- glad.c
|-- glm/
|   +-- glm/
|       |-- glm.hpp
|       +-- ...
+-- stb/
    +-- stb_image.h
```

#### GLFW

Clone GLFW into `external/glfw/`:

```bash
git clone https://github.com/glfw/glfw.git external/glfw
```

Or download a release from [glfw.org](https://www.glfw.org/download.html).

#### GLM

**Option 1: Git clone**
```bash
git clone https://github.com/g-truc/glm.git external/glm
```

**Option 2: Download release**
1. Download from [GitHub Releases](https://github.com/g-truc/glm/releases)
2. Extract so headers are at `external/glm/glm/glm.hpp`

#### GLAD (OpenGL Loader)

GLAD must be generated for your specific OpenGL version:

1. Go to [GLAD Generator](https://glad.dav1d.de/)
2. Configure:
   - **Language:** C/C++
   - **Specification:** OpenGL
   - **API gl:** Version 4.6
   - **Profile:** Core
   - **Options:** Check "Generate a loader"
3. Click **Generate**
4. Download the zip file
5. Extract contents to `external/glad/`:
   ```
   external/glad/
   |-- include/
   |   |-- glad/
   |   |   +-- glad.h
   |   +-- KHR/
   |       +-- khrplatform.h
   +-- src/
       +-- glad.c
   ```

#### stb_image

Download the single-header library using PowerShell:

```powershell
mkdir external\stb -Force
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" -OutFile "external\stb\stb_image.h"
```

## Optional: Vulkan SDK

For Vulkan rendering support:

### Windows

1. Download from [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
2. Run the installer
3. Verify installation:
   ```cmd
   glslangValidator --version
   ```

## Optional: FreeType

For text rendering support:

### Windows

FreeType is bundled or can be installed via vcpkg:
```cmd
vcpkg install freetype:x64-windows
```

## Verifying Setup

After installing dependencies, verify your setup (run in Developer Command Prompt):

```cmd
:: Check CMake
cmake --version

:: Check compiler
cl

:: Check Vulkan (optional)
vulkaninfo

:: Check GLSL compiler (optional)
glslangValidator --version
```

## Troubleshooting

### "Could not find GLFW"

CMake cannot find GLFW. Solutions:
1. Ensure GLFW is in `external/glfw/`
2. Check that `external/glfw/CMakeLists.txt` exists

### "Could not find GLM"

CMake cannot find GLM. Solutions:
1. Ensure GLM is in `external/glm/`
2. Check that `external/glm/glm/glm.hpp` exists

### "glad.h not found"

GLAD was not properly set up:
1. Regenerate GLAD at [glad.dav1d.de](https://glad.dav1d.de/)
2. Ensure files are in `external/glad/include/glad/glad.h`
3. Ensure `external/glad/src/glad.c` exists

### "stb_image.h not found"

1. Download stb_image.h manually
2. Place it in `external/stb/stb_image.h`

### Vulkan Validation Layers Missing

If you see validation layer warnings:
1. Reinstall Vulkan SDK
2. Ensure `VK_LAYER_PATH` environment variable is set
3. Run `vulkaninfo` to check that validation layers are installed

## Next Steps

After setting up dependencies:

1. [Build the project](BUILDING.md)
2. [Learn the editor](EDITOR.md)
3. [Understand the architecture](ARCHITECTURE.md)

## Platform-Specific Notes

### Windows

- Use **x64 Native Tools Command Prompt** for command-line builds
- Visual Studio 2019+ recommended for IDE development
- PowerShell execution policy may need adjustment for scripts:
  ```powershell
  Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
  ```

## See Also

- [Building Guide](BUILDING.md) - Compiling the project
- [Architecture](ARCHITECTURE.md) - Understanding the codebase
