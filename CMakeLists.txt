cmake_minimum_required(VERSION 3.10)

# Try to use vcpkg toolchain if available (MUST be before project() call)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        message(STATUS "Using vcpkg toolchain from VCPKG_ROOT: $ENV{VCPKG_ROOT}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        message(STATUS "Using vcpkg toolchain from project directory")
    endif()
endif()

project(rift)

# Parse version from Version.hpp
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.hpp" _ver_major REGEX "#define RIFT_VERSION_MAJOR")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.hpp" _ver_minor REGEX "#define RIFT_VERSION_MINOR")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.hpp" _ver_patch REGEX "#define RIFT_VERSION_PATCH")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.hpp" _ver_release REGEX "#define RIFT_VERSION_RELEASE")
string(REGEX REPLACE ".*#define RIFT_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" RIFT_VERSION_MAJOR "${_ver_major}")
string(REGEX REPLACE ".*#define RIFT_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" RIFT_VERSION_MINOR "${_ver_minor}")
string(REGEX REPLACE ".*#define RIFT_VERSION_PATCH[ \t]+([0-9]+).*" "\\1" RIFT_VERSION_PATCH "${_ver_patch}")
string(REGEX REPLACE ".*#define RIFT_VERSION_RELEASE[ \t]+([0-9]+).*" "\\1" RIFT_VERSION_RELEASE "${_ver_release}")
set(RIFT_VERSION_STRING "${RIFT_VERSION_MAJOR}.${RIFT_VERSION_MINOR}.${RIFT_VERSION_PATCH}.${RIFT_VERSION_RELEASE}")
message(STATUS "Rift version: ${RIFT_VERSION_STRING}")

# Generate Doxyfile from template
configure_file("${CMAKE_SOURCE_DIR}/Doxyfile.in" "${CMAKE_BINARY_DIR}/Doxyfile" @ONLY)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# clang-tidy
# ============================================================================
# Three ways to invoke clang-tidy:
#   * `.\build.bat` step 3 - drives a Ninja sidecar build (build-cdb/) via the
#     `compile-db` preset to emit a real compile_commands.json, then lints
#     src/*.cpp and tests/*.cpp per file. Blocking - the pipeline stops on
#     diagnostics.
#   * `cmake --build build --target tidy` - lints all project sources using
#     the main build tree's compile_commands.json (handy for one-off runs).
#   * `-DRIFT_ENABLE_CLANG_TIDY=ON` at configure time - run clang-tidy as part
#     of every C++ compilation (much slower; off by default).
# .clang-tidy at the repo root defines the enabled checks. clang-format still
# owns formatting; clang-tidy here only does semantic checks.
option(RIFT_ENABLE_CLANG_TIDY "Run clang-tidy on every C++ compilation (slow)" OFF)
find_program(RIFT_CLANG_TIDY_EXE NAMES clang-tidy)

if(RIFT_CLANG_TIDY_EXE)
    if(RIFT_ENABLE_CLANG_TIDY)
        set(CMAKE_CXX_CLANG_TIDY "${RIFT_CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy build-time integration enabled: ${RIFT_CLANG_TIDY_EXE}")
    else()
        message(STATUS "clang-tidy found: ${RIFT_CLANG_TIDY_EXE}"
                       " (run `cmake --build ${CMAKE_BINARY_DIR} --target tidy`)")
    endif()
else()
    message(STATUS "clang-tidy not found - static analysis target will no-op."
                   " Install LLVM to enable.")
endif()

# MSVC: Enable asynchronous exception handling for SEH translation support
if(MSVC)
    string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    # /Zc:__cplusplus so the __cplusplus macro reports the real value; the
    # bundled ecs.hpp (C++23, std::expected) relies on it under MSVC.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
endif()

# Always compile both renderers - switching is done at runtime
message(STATUS "Building with both OpenGL and Vulkan support (runtime switchable)")

# Find OpenGL (always required)
find_package(OpenGL REQUIRED)
add_definitions(-DUSE_OPENGL)

# Find Vulkan (required - the Vulkan renderer is always built and linked)
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")

# GLM - try vcpkg first, otherwise use bundled
find_package(glm CONFIG QUIET)
if(glm_FOUND)
    message(STATUS "GLM found via vcpkg/find_package")
    set(GLM_FROM_VCPKG TRUE)
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/glm/glm/glm.hpp")
        include_directories(${CMAKE_SOURCE_DIR}/external/glm)
        message(STATUS "Using bundled GLM from external/")
    else()
        message(FATAL_ERROR "GLM not found!\n"
                "Install via vcpkg: vcpkg install glm\n"
                "Or run setup-dependencies.ps1 to download to external/")
    endif()
endif()

# GLFW - try vcpkg first, otherwise use bundled
find_package(glfw3 CONFIG QUIET)
if(glfw3_FOUND)
    message(STATUS "GLFW found via vcpkg/find_package")
    set(GLFW_FROM_VCPKG TRUE)
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/glfw/CMakeLists.txt")
        add_subdirectory(external/glfw)
        message(STATUS "Using bundled GLFW from external/")
    else()
        message(FATAL_ERROR "GLFW not found!\n"
                "Install via vcpkg: vcpkg install glfw3\n"
                "Or run setup-dependencies.ps1 to download to external/")
    endif()
endif()

# GLAD - try vcpkg first, otherwise use bundled
find_package(glad CONFIG QUIET)
if(glad_FOUND)
    message(STATUS "GLAD found via vcpkg/find_package")
    set(GLAD_FROM_VCPKG TRUE)
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/glad/src/glad.c")
        add_subdirectory(external/glad)
        message(STATUS "Using bundled GLAD from external/")
    else()
        message(FATAL_ERROR "GLAD not found!\n"
                "Install via vcpkg: vcpkg install glad[gl-api-33-core,loader]\n"
                "Or run setup-dependencies.ps1 to download to external/")
    endif()
endif()

# stb - try vcpkg first, otherwise use bundled
find_package(Stb QUIET)
if(Stb_FOUND)
    message(STATUS "stb found via vcpkg/find_package")
    set(STB_FROM_VCPKG TRUE)
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/stb/stb_image.h")
        include_directories(${CMAKE_SOURCE_DIR}/external/stb)
        message(STATUS "Using bundled stb from external/")
    else()
        message(FATAL_ERROR "stb not found!\n"
                "Install via vcpkg: vcpkg install stb\n"
                "Or run setup-dependencies.ps1 to download to external/")
    endif()
endif()

# nlohmann/json - try vcpkg first, otherwise use bundled
find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann/json found via vcpkg/find_package")
    set(JSON_FROM_VCPKG TRUE)
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/nlohmann/json.hpp")
        include_directories(${CMAKE_SOURCE_DIR}/external/nlohmann)
        message(STATUS "Using bundled nlohmann/json from external/")
    else()
        message(FATAL_ERROR "nlohmann/json not found!\n"
                "Install via vcpkg: vcpkg install nlohmann-json\n"
                "Or run setup-dependencies.ps1 to download to external/")
    endif()
endif()

# ecs - header-only ECS. Try vcpkg first, otherwise use bundled. STL-only, so
# the bundled path is just an include dir (no link target). Requires C++23.
find_package(ecs CONFIG QUIET)
if(ecs_FOUND)
    message(STATUS "ecs found via vcpkg/find_package")
    set(ECS_FROM_VCPKG TRUE)
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/ecs/ecs.hpp")
        include_directories(${CMAKE_SOURCE_DIR}/external/ecs)
        message(STATUS "Using bundled ecs from external/")
    else()
        message(FATAL_ERROR "ecs not found!\n"
                "Run setup.ps1 to copy ecs.hpp into external/ecs/")
    endif()
endif()

# Include directories
if(EXISTS "${CMAKE_SOURCE_DIR}/include")
    include_directories(${CMAKE_SOURCE_DIR}/include)
endif()
if(NOT GLAD_FROM_VCPKG AND EXISTS "${CMAKE_SOURCE_DIR}/external/glad/include")
    include_directories(${CMAKE_SOURCE_DIR}/external/glad/include)
endif()
if(NOT GLFW_FROM_VCPKG AND EXISTS "${CMAKE_SOURCE_DIR}/external/glfw/include")
    include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
endif()

# FreeType (for TTF font rendering)
find_package(Freetype QUIET)
if(Freetype_FOUND)
    message(STATUS "FreeType found: ${FREETYPE_INCLUDE_DIRS}")
    add_definitions(-DUSE_FREETYPE)
else()
    message(WARNING "FreeType not found! Text rendering will not work.\n"
                    "To install FreeType:\n"
                    "  Windows (vcpkg): vcpkg install freetype\n"
                    "  Linux: sudo apt-get install libfreetype6-dev\n"
                    "  macOS: brew install freetype\n"
                    "\n"
                    "If using vcpkg, configure CMake with:\n"
                    "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake")
    # Don't fail, but text rendering won't work
endif()

# Source files - always include all renderer files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Standalone clang-tidy target: runs the configured checks across project
# sources using the compile_commands.json produced above. Does not rebuild.
if(RIFT_CLANG_TIDY_EXE)
    file(GLOB RIFT_TIDY_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
    add_custom_target(tidy
        COMMAND ${RIFT_CLANG_TIDY_EXE}
                -p ${CMAKE_BINARY_DIR}
                --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
                ${RIFT_TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy on src/*.cpp (project headers via HeaderFilterRegex)"
        VERBATIM)
else()
    add_custom_target(tidy
        COMMAND ${CMAKE_COMMAND} -E echo
                "clang-tidy not found - skipping static analysis."
        COMMENT "clang-tidy target (no-op: tool missing)")
endif()

# Note: stb_image implementation is included directly in Texture.cpp via
# #define STB_IMAGE_IMPLEMENTATION, so we don't compile external/stb/*.c separately
set(STB_SOURCES "")

# Executable
add_executable(${PROJECT_NAME} ${SOURCES} ${STB_SOURCES})

# Link OpenGL (always required)
target_link_libraries(${PROJECT_NAME} OpenGL::GL)

# Link GLFW (vcpkg and bundled both use 'glfw' target)
target_link_libraries(${PROJECT_NAME} glfw)

# Link GLAD
if(GLAD_FROM_VCPKG)
    target_link_libraries(${PROJECT_NAME} glad::glad)
else()
    target_link_libraries(${PROJECT_NAME} glad)
endif()

# Link GLM if from vcpkg (header-only but has target)
if(GLM_FROM_VCPKG)
    target_link_libraries(${PROJECT_NAME} glm::glm)
endif()

# Link nlohmann_json if from vcpkg (header-only but has target)
if(JSON_FROM_VCPKG)
    target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)
endif()

# Link ecs if from vcpkg (header-only INTERFACE target; bundled path uses
# the global include dir and needs no link)
if(ECS_FROM_VCPKG)
    target_link_libraries(${PROJECT_NAME} ecs)
endif()

# Link stb include dirs if from vcpkg
if(STB_FROM_VCPKG)
    target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})
endif()

# Link Vulkan (always required)
target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARIES})
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

# Link FreeType if available
if(Freetype_FOUND)
    target_link_libraries(${PROJECT_NAME} ${FREETYPE_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
endif()

# Directories for runtime assets
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
set(SHADERS_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(PROJECT_MANIFEST_FILE "${CMAKE_SOURCE_DIR}/rift.project.json")

# Copy entire assets and shaders folders to build root + Debug + Release
foreach(subdir "" "Debug" "Release")
    if(subdir STREQUAL "")
        set(OUT_DIR "${CMAKE_BINARY_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/${subdir}")
    endif()

    # Create destination directory if it doesn't exist
    file(MAKE_DIRECTORY "${OUT_DIR}")

    if(EXISTS "${ASSETS_DIR}")
        file(COPY "${ASSETS_DIR}" DESTINATION "${OUT_DIR}")
    endif()
    if(EXISTS "${SHADERS_DIR}")
        file(COPY "${SHADERS_DIR}" DESTINATION "${OUT_DIR}")
    endif()
    if(EXISTS "${PROJECT_MANIFEST_FILE}")
        file(COPY "${PROJECT_MANIFEST_FILE}" DESTINATION "${OUT_DIR}")
    endif()
endforeach()

# ============================================================================
# Shader compilation (SPIR-V)
# ============================================================================
find_program(GLSLANG_VALIDATOR glslangValidator)

set(SHADER_SOURCES
    "${SHADERS_DIR}/Geometry.vert"
    "${SHADERS_DIR}/Geometry.frag"
)

if(GLSLANG_VALIDATOR)
    message(STATUS "glslangValidator found: ${GLSLANG_VALIDATOR}")
    message(STATUS "-> Shaders will be compiled to SPIR-V during build")

    set(SHADER_OUTPUTS "")
    foreach(SHADER_SRC ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
        set(SHADER_SPV "${SHADERS_DIR}/${SHADER_NAME}.spv")
        list(APPEND SHADER_OUTPUTS ${SHADER_SPV})

        add_custom_command(
            OUTPUT ${SHADER_SPV}
            COMMAND ${GLSLANG_VALIDATOR} -V -DUSE_VULKAN "${SHADER_SRC}" -o "${SHADER_SPV}"
            DEPENDS ${SHADER_SRC}
            COMMENT "Compiling ${SHADER_NAME} -> ${SHADER_NAME}.spv"
            VERBATIM
        )
    endforeach()

    add_custom_target(compile_shaders DEPENDS ${SHADER_OUTPUTS})
else()
    message(STATUS "glslangValidator not found - shader compilation skipped")
    message(STATUS "  Pre-compiled .spv files will be copied if present")
    # No-op target so add_dependencies below always works
    add_custom_target(compile_shaders)
endif()

# Show shader sources in IDE project tree
file(GLOB SHADER_FILES "${SHADERS_DIR}/*.vert" "${SHADERS_DIR}/*.frag" "${SHADERS_DIR}/*.glsl")
if(SHADER_FILES)
    target_sources(${PROJECT_NAME} PRIVATE ${SHADER_FILES})
    source_group("Shaders" FILES ${SHADER_FILES})
endif()

# Ensure shaders are compiled before the main target builds
add_dependencies(${PROJECT_NAME} compile_shaders)

# Post-build: copy assets and shaders next to the executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_DIR}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_DIR}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_MANIFEST_FILE}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/rift.project.json"
    COMMENT "Copying assets and shaders to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Copy save files from project root to build directories
file(GLOB SAVE_FILES "${CMAKE_SOURCE_DIR}/rift.save*.json")
if(SAVE_FILES)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying save files to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SAVE_FILES} "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()


# ============================================================================
# Testing Support
# ============================================================================
# Tests are configured into the same `build/` tree as the game so a single
# `cmake --preset default` covers both. Build a specific target to skip the
# other; e.g. `cmake --build build --target rift` for game-only iteration.
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    # Google Test - try vcpkg first, otherwise use FetchContent
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        message(STATUS "Google Test found via vcpkg/find_package")
        set(GTEST_FROM_VCPKG TRUE)
    else()
        message(STATUS "Google Test not found via vcpkg, fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
        )
        # Prevent overriding parent project's compiler/linker settings on Windows
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        message(STATUS "Using Google Test from FetchContent")
    endif()

    # Collect test source files
    file(GLOB TEST_SOURCES "tests/*.cpp")

    # Source files needed for tests (exclude main.cpp).
    # CollisionSystemTests exercises the collision pipeline end-to-end, so
    # it requires Tilemap + the collision/movement systems + their transitive
    # deps. The test binary is linked against the same graphics libraries as the
    # game but never creates a GL/VK context, so rendering methods are not called
    # at runtime. Tests should not invoke render paths.
    set(TEST_LIB_SOURCES
        "${CMAKE_SOURCE_DIR}/src/Logger.cpp"
        "${CMAKE_SOURCE_DIR}/src/TimeManager.cpp"
        # WeatherDefinitions: pure data table + light-intensity math.
        # No renderer dependency; tests exercise GetWeatherDefinition and
        # ComputeLightIntensity directly.
        "${CMAKE_SOURCE_DIR}/src/WeatherDefinitions.cpp"
        # DrawTracer: tiny per-frame event log; renderer-free. Linked here
        # so ConsoleCommands.cpp's Cmd_RendererTrace resolves at test link.
        "${CMAKE_SOURCE_DIR}/src/DrawTracer.cpp"
        "${CMAKE_SOURCE_DIR}/src/CollisionSystem.cpp"
        # MotionSystem: stateless momentum kinematics over a Motor component
        # (the former PlayerMotor class is deleted); renderer-free.
        "${CMAKE_SOURCE_DIR}/src/MotionSystem.cpp"
        # PlayerMovementSystem: stateless player move/animation/facing logic over
        # component bundles (PlayerCharacter the class is dissolved; the player is
        # granular components + free systems).
        "${CMAKE_SOURCE_DIR}/src/PlayerMovementSystem.cpp"
        # PlayerSystem: player appearance/mode/per-frame glue over components,
        # services from globals (replaces PlayerCharacter's non-render methods).
        "${CMAKE_SOURCE_DIR}/src/PlayerSystem.cpp"
        # NpcAiSystem: stateless NPC patrol/idle AI over component bundles
        # (NonPlayerCharacter the class is dissolved; NPCs are granular components).
        "${CMAKE_SOURCE_DIR}/src/NpcAiSystem.cpp"
        # NpcRender / PlayerRender: free-function sprite draw over components; only
        # use the virtual IRenderer interface (replace the entity render methods).
        "${CMAKE_SOURCE_DIR}/src/NpcRender.cpp"
        "${CMAKE_SOURCE_DIR}/src/PlayerRender.cpp"
        # RenderDrawable: AddNpcDrawables (ECS render-list build); needs NpcRender.
        "${CMAKE_SOURCE_DIR}/src/RenderDrawable.cpp"
        # EntityStore: NPC spawn/lifecycle/query seam over the registry; renderer-free
        # (SpawnNpc only touches an optional IRenderer for the upload path).
        "${CMAKE_SOURCE_DIR}/src/EntityStore.cpp"
        "${CMAKE_SOURCE_DIR}/src/PatrolRoute.cpp"
        # Pathfinding: free-function BFS over Tilemap navigation; renderer-free.
        # Used by Cmd_NavPath / Cmd_NavReachable.
        "${CMAKE_SOURCE_DIR}/src/Pathfinding.cpp"
        # CharacterKinematics: stateless elevation + walk-animation logic over the
        # Elevation/AnimationState components, extracted from GameCharacter.
        "${CMAKE_SOURCE_DIR}/src/CharacterKinematics.cpp"
        "${CMAKE_SOURCE_DIR}/src/Tilemap.cpp"
        "${CMAKE_SOURCE_DIR}/src/Texture.cpp"
        # TextureStore: renderer-side owner of sprite textures keyed by handle;
        # data paths are renderer-free (UploadAll needs only a virtual IRenderer).
        "${CMAKE_SOURCE_DIR}/src/TextureStore.cpp"
        # DialogueStore: owner of NPC dialogue trees keyed by handle; pure data,
        # referenced by NonPlayerCharacter/Tilemap/ConsoleCommands at test link.
        "${CMAKE_SOURCE_DIR}/src/DialogueStore.cpp"
        # CharacterRender: shared character sprite draw mechanics; only uses the
        # virtual IRenderer interface, referenced by Player/NonPlayerCharacter.
        "${CMAKE_SOURCE_DIR}/src/CharacterRender.cpp"
        # AssetRegistry: character/NPC sprite asset paths (demoted class statics);
        # pure data, referenced by Player/NonPlayerCharacter/Tilemap/ConsoleCommands.
        "${CMAKE_SOURCE_DIR}/src/AssetRegistry.cpp"
        "${CMAKE_SOURCE_DIR}/src/IRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/EditorCommands.cpp"
        "${CMAKE_SOURCE_DIR}/src/NavigationRecalc.cpp"
        "${CMAKE_SOURCE_DIR}/src/ProjectManifest.cpp"
        # ParticleSystem: ambient spawn logic is renderer-free; tests exercise
        # it via Update(...) without ever calling LoadTextures/Render.
        "${CMAKE_SOURCE_DIR}/src/ParticleSystem.cpp"
        # Console: data layer + free command functions are renderer-free.
        # ConsoleCommands.cpp's Console::RegisterDefaultCommands references
        # Game internals via friendship; tests do not call it (they wire
        # CommandContext by hand), so the symbol is unreferenced and the
        # linker drops it.
        "${CMAKE_SOURCE_DIR}/src/Console.cpp"
        "${CMAKE_SOURCE_DIR}/src/ConsoleCommands.cpp"
        # DialogueManager: required by Cmd_DialogueSkip's production wrapper
        # and by ConsoleCommandsTests' DialogueActiveReportsInactive test.
        # Implementation is renderer-free.
        "${CMAKE_SOURCE_DIR}/src/DialogueManager.cpp"
        "${CMAKE_SOURCE_DIR}/src/GameStateManager.cpp"
        # Editor + CameraController: needed by ConsoleCommandsTests for the
        # editor / debug.* / globe.* commands. Editor::Render() in Editor.cpp
        # references the overlay rendering methods defined in
        # EditorRendering.cpp; both are linked but never invoked at runtime.
        # IRenderer methods they call are virtual and dispatched at runtime,
        # so no concrete renderer is needed. EditorInput.cpp is intentionally
        # excluded - ProcessMouseInput pulls in the dialogue-tree builders.
        "${CMAKE_SOURCE_DIR}/src/Editor.cpp"
        "${CMAKE_SOURCE_DIR}/src/EditorRendering.cpp"
        "${CMAKE_SOURCE_DIR}/src/CameraController.cpp"
        # AuroraTextures: pure procedural RGBA pixel-buffer builders for the
        # AuroraNight effect; renderer-free, exercised directly by tests.
        "${CMAKE_SOURCE_DIR}/src/AuroraTextures.cpp"
    )

    # Create test executable
    add_executable(rift_tests ${TEST_SOURCES} ${TEST_LIB_SOURCES})

    target_include_directories(rift_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    # Link Google Test
    if(GTEST_FROM_VCPKG)
        target_link_libraries(rift_tests PRIVATE GTest::gtest GTest::gtest_main)
    else()
        target_link_libraries(rift_tests PRIVATE gtest gtest_main)
    endif()

    # Link graphics libraries used by Tilemap/Texture/etc. Tests don't create
    # a GL/VK context, so these are only needed to resolve link-time symbols.
    target_link_libraries(rift_tests PRIVATE OpenGL::GL glfw)

    if(GLAD_FROM_VCPKG)
        target_link_libraries(rift_tests PRIVATE glad::glad)
    else()
        target_link_libraries(rift_tests PRIVATE glad)
    endif()

    if(GLM_FROM_VCPKG)
        target_link_libraries(rift_tests PRIVATE glm::glm)
    endif()

    if(JSON_FROM_VCPKG)
        target_link_libraries(rift_tests PRIVATE nlohmann_json::nlohmann_json)
    endif()

    if(ECS_FROM_VCPKG)
        target_link_libraries(rift_tests PRIVATE ecs)
    endif()

    if(STB_FROM_VCPKG)
        target_include_directories(rift_tests PRIVATE ${Stb_INCLUDE_DIR})
    endif()

    target_link_libraries(rift_tests PRIVATE ${Vulkan_LIBRARIES})
    target_include_directories(rift_tests PRIVATE ${Vulkan_INCLUDE_DIRS})

    if(Freetype_FOUND)
        target_link_libraries(rift_tests PRIVATE ${FREETYPE_LIBRARIES})
        target_include_directories(rift_tests PRIVATE ${FREETYPE_INCLUDE_DIRS})
    endif()

    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(rift_tests)

    message(STATUS "Tests enabled - build target: rift_tests")
endif()
