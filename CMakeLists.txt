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

# Parse version from Version.h
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.h" _ver_major REGEX "#define RIFT_VERSION_MAJOR")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.h" _ver_minor REGEX "#define RIFT_VERSION_MINOR")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.h" _ver_patch REGEX "#define RIFT_VERSION_PATCH")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/Version.h" _ver_release REGEX "#define RIFT_VERSION_RELEASE")
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

# MSVC: Enable asynchronous exception handling for SEH translation support
if(MSVC)
    string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Always compile both renderers - switching is done at runtime
message(STATUS "Building with both OpenGL and Vulkan support (runtime switchable)")

# Find OpenGL (always required)
find_package(OpenGL REQUIRED)
add_definitions(-DUSE_OPENGL)

# Find Vulkan (optional - if not found, Vulkan renderer won't be available)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_definitions(-DUSE_VULKAN)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "-> Vulkan renderer will be available at runtime")
else()
    message(STATUS "Vulkan SDK not found - Vulkan renderer will NOT be available")
    message(STATUS "-> To enable Vulkan, install the Vulkan SDK from https://vulkan.lunarg.com/sdk/home")
endif()

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

# Note: stb_image implementation is included directly in Texture.cpp via
# #define STB_IMAGE_IMPLEMENTATION, so we don't compile external/stb/*.c separately
set(STB_SOURCES "")

# Exclude Vulkan files only if Vulkan SDK is not available
if(NOT Vulkan_FOUND)
    list(REMOVE_ITEM SOURCES
        "${CMAKE_SOURCE_DIR}/src/VulkanRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/VulkanRendererBuffers.cpp"
        "${CMAKE_SOURCE_DIR}/src/VulkanRendererHelpers.cpp"
        "${CMAKE_SOURCE_DIR}/src/VulkanShader.cpp"
    )
endif()

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

# Link stb include dirs if from vcpkg
if(STB_FROM_VCPKG)
    target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})
endif()

# Link Vulkan if available
if(Vulkan_FOUND)
    target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

# Link FreeType if available
if(Freetype_FOUND)
    target_link_libraries(${PROJECT_NAME} ${FREETYPE_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
endif()

# Directories for runtime assets
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
set(SHADERS_DIR "${CMAKE_SOURCE_DIR}/shaders")

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
endforeach()

# Collect shader files for dependency tracking
file(GLOB SHADER_FILES "${SHADERS_DIR}/*.vert" "${SHADERS_DIR}/*.frag" "${SHADERS_DIR}/*.glsl" "${SHADERS_DIR}/*.spv")

# Custom target to copy shader folder contents - runs on every build
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Debug/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Release/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_DIR}" "${CMAKE_BINARY_DIR}/Debug/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_DIR}" "${CMAKE_BINARY_DIR}/Release/shaders"
    COMMENT "Copying shader files to Debug and Release directories"
    SOURCES ${SHADER_FILES}
)

# Make main target depend on shader copying
add_dependencies(${PROJECT_NAME} copy_shaders)

# Post-build step: make sure assets and shaders sit next to the executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copying assets to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_DIR}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMAND ${CMAKE_COMMAND} -E echo "Copying shaders to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_DIR}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Copy save files from project root to build directories
file(GLOB SAVE_FILES "${CMAKE_SOURCE_DIR}/save*.json")
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
option(BUILD_TESTS "Build unit tests" OFF)

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

    # Source files needed for tests (exclude main.cpp)
    set(TEST_LIB_SOURCES
        "${CMAKE_SOURCE_DIR}/src/TimeManager.cpp"
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

    # Link GLM (required for TimeManager)
    if(GLM_FROM_VCPKG)
        target_link_libraries(rift_tests PRIVATE glm::glm)
    endif()

    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(rift_tests)

    message(STATUS "Tests enabled - build target: rift_tests")
endif()
