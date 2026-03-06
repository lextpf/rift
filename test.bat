@echo off
REM ============================================================================
REM test.bat - Run Rift Engine unit tests using Google Test
REM ============================================================================
REM This script:
REM   1. Configures CMake if needed
REM   2. Builds the test executable
REM   3. Runs all Google Test executables
REM ============================================================================

setlocal

echo ============================================================================
echo                          RIFT ENGINE TEST RUNNER
echo ============================================================================
echo.

REM ============================================================================
REM STEP 1: Configure CMake
REM ============================================================================
echo [1/3] Checking CMake configuration...
echo ----------------------------------------------------------------------------

REM Check if vcpkg is available
if defined VCPKG_ROOT (
    echo   Using vcpkg from: %VCPKG_ROOT%
    set TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
    REM Prefer installed tools (git/cmake/ninja) over downloading PortableGit.
    set VCPKG_FORCE_SYSTEM_BINARIES=1
) else if exist "%~dp0vcpkg\scripts\buildsystems\vcpkg.cmake" (
    echo   Using vcpkg from project directory
    set TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%~dp0vcpkg/scripts/buildsystems/vcpkg.cmake
    REM Prefer installed tools (git/cmake/ninja) over downloading PortableGit.
    set VCPKG_FORCE_SYSTEM_BINARIES=1
) else (
    echo   Warning: vcpkg not found, using bundled dependencies from external/
    set TOOLCHAIN=
)

REM Create build directory for tests
if not exist "build_tests" mkdir build_tests
cd build_tests

if not exist "CMakeCache.txt" (
    echo   Configuring CMake...
    cmake .. -DBUILD_TESTS=ON %TOOLCHAIN%
    if errorlevel 1 (
        echo ERROR: CMake configuration failed!
        cd ..
        pause
        exit /b 1
    )
) else (
    echo   Found existing configuration
)
echo.

REM ============================================================================
REM STEP 2: Build Tests
REM ============================================================================
echo [2/3] Building test executables...
echo ----------------------------------------------------------------------------
cmake --build . --config Release --target rift_tests
if errorlevel 1 (
    echo ERROR: Build failed!
    cd ..
    pause
    exit /b 1
)
echo.

REM ============================================================================
REM STEP 3: Run Tests
REM ============================================================================
echo [3/3] Running tests...
echo ----------------------------------------------------------------------------
echo.

set ALL_PASSED=1

REM Run rift_tests
echo === rift_tests ===
if exist "Release\rift_tests.exe" (
    Release\rift_tests.exe --gtest_color=yes
    if errorlevel 1 set ALL_PASSED=0
) else if exist "rift_tests.exe" (
    rift_tests.exe --gtest_color=yes
    if errorlevel 1 set ALL_PASSED=0
) else (
    echo ERROR: rift_tests.exe not found!
    set ALL_PASSED=0
)
echo.

cd ..

REM ============================================================================
REM SUMMARY
REM ============================================================================
echo ============================================================================
if %ALL_PASSED%==1 (
    echo                            ALL TESTS PASSED
) else (
    echo                            SOME TESTS FAILED
)
echo ============================================================================

pause
if %ALL_PASSED%==1 (
    exit /b 0
) else (
    exit /b 1
)
