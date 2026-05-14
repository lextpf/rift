@echo off
REM ============================================================================
REM test.bat - Run Rift unit tests using Google Test
REM ============================================================================
REM This script:
REM   1. Configures CMake if needed (shares build/ with the game)
REM   2. Builds the rift_tests target
REM   3. Runs all Google Test executables
REM ============================================================================

setlocal

echo ============================================================================
echo                             RIFT TEST RUNNER
echo ============================================================================
echo.

REM ============================================================================
REM STEP 1: Configure CMake
REM ============================================================================
echo [1/3] Checking CMake configuration...
echo ----------------------------------------------------------------------------
if not exist "build\CMakeCache.txt" (
    echo   Configuring CMake...
    cmake --preset default
    if errorlevel 1 (
        echo ERROR: CMake configuration failed!
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
echo [2/3] Building test executable...
echo ----------------------------------------------------------------------------
cmake --build build --config Release --target rift_tests
if errorlevel 1 (
    echo ERROR: Build failed!
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
if exist "build\Release\rift_tests.exe" (
    build\Release\rift_tests.exe --gtest_color=yes
    if errorlevel 1 set ALL_PASSED=0
) else if exist "build\rift_tests.exe" (
    build\rift_tests.exe --gtest_color=yes
    if errorlevel 1 set ALL_PASSED=0
) else (
    echo ERROR: rift_tests.exe not found!
    set ALL_PASSED=0
)
echo.

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
