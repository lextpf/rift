@echo off
REM ===========================================================================================
REM run.bat - Run rift: the Release build of the game
REM ===========================================================================================
REM Starts build\Release\rift.exe with the repository root as the working directory and waits
REM for it to exit. The game inherits this console, so its log lands in this window and Ctrl-C
REM stops it.
REM
REM rift.exe takes no command-line arguments - runtime settings are developer-console commands.
REM ===========================================================================================

setlocal

echo ============================================================================
echo                                    RIFT
echo ============================================================================
echo.

set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

set "GAME_DIR=%REPO_ROOT%\build\Release"
set "GAME_EXE=%GAME_DIR%\rift.exe"
set "MANIFEST=%REPO_ROOT%\rift.project.json"

REM ============================================================================
REM Preflight
REM ============================================================================
if not exist "%GAME_EXE%" (
    echo ERROR: rift.exe not found at
    echo   %GAME_EXE%
    echo.
    echo Build it first:
    echo   build.bat                full pipeline ^(format, configure, tidy, build, docs^)
    echo   build.bat --skip-tidy    the same without the blocking static-analysis step
    echo.
    echo Or build only the Release game:
    echo   cmake --preset default
    echo   cmake --build build --config Release --target rift
    exit /b 1
)

if not exist "%MANIFEST%" (
    echo WARNING: rift.project.json not found in the repo root.
    echo          rift.exe will look one directory further up and, failing that, fall back to
    echo          its built-in asset defaults - almost certainly not what you want.
    echo.
)

if not exist "%REPO_ROOT%\assets" (
    echo WARNING: assets\ not found in the repo root.
    echo          Assets are not part of the repository. Without them the manifest fails
    echo          validation on its tilesets and player sprites, and startup aborts.
    echo.
)

if not exist "%REPO_ROOT%\shaders" (
    echo WARNING: shaders\ not found in the repo root.
    echo          The OpenGL backend resolves its shader sources from there.
    echo.
)

REM build.bat and run.bat share one build tree, so an executable from before the last source
REM edit is easy to end up with - and a stale run looks exactly like a fix that did not work.
REM The check degrades to silence when powershell is unavailable.
set "STALE="
for /f %%R in ('powershell -NoProfile -Command "$e=(Get-Item '%GAME_EXE%').LastWriteTime;$n=[datetime]::MinValue;foreach($f in Get-ChildItem '%REPO_ROOT%\src','%REPO_ROOT%\shaders' -File -Recurse -ErrorAction SilentlyContinue){if($f.LastWriteTime -gt $n){$n=$f.LastWriteTime}};[int]($n -gt $e)"') do set "STALE=%%R"
if "%STALE%"=="1" (
    echo WARNING: src\ or shaders\ is newer than rift.exe - this Release build is stale.
    echo          Run build.bat to rebuild, or carry on to run the older executable.
    echo.
)

REM The manifest picks the startup backend, so report it: an unexpected Vulkan start otherwise
REM only shows up as different behaviour once the window is already open.
set "STARTUP_RENDERER="
if exist "%MANIFEST%" (
    for /f "tokens=2 delims=:" %%r in ('findstr /c:"startupRenderer" "%MANIFEST%"') do set "STARTUP_RENDERER=%%r"
)
set "STARTUP_RENDERER=%STARTUP_RENDERER: =%"
set "STARTUP_RENDERER=%STARTUP_RENDERER:,=%"
if defined STARTUP_RENDERER set STARTUP_RENDERER=%STARTUP_RENDERER:"=%

REM ============================================================================
REM Launch
REM ============================================================================
echo Executable:        %GAME_EXE%
echo Working directory: %REPO_ROOT%
if defined STARTUP_RENDERER echo Startup renderer:  %STARTUP_RENDERER%
echo.
echo   F12 opens the developer console. 'help' lists the commands, 'ed' toggles the
echo   editor, and 'renderer.set opengl' / 'renderer.set vulkan' hot-swaps the backend.
echo.
echo ----------------------------------------------------------------------------

cd /d "%REPO_ROOT%"
"%GAME_EXE%"
set "GAME_RC=%ERRORLEVEL%"

REM ============================================================================
REM Summary
REM ============================================================================
echo ----------------------------------------------------------------------------
if not "%GAME_RC%"=="0" goto :game_failed

echo.
echo ============================================================================
echo                            RIFT EXITED CLEANLY
echo ============================================================================

endlocal
exit /b 0

REM ============================================================================
REM Reached only by GOTO from the summary
REM ============================================================================
:game_failed
echo.
echo ============================================================================
echo                       RIFT EXITED WITH CODE %GAME_RC%
echo ============================================================================
echo.
echo   Crashes and startup failures are appended to
echo     %REPO_ROOT%\rift.project.log
echo   Manifest problems are printed above, before the window opens.
echo.
pause

REM endlocal discards GAME_RC, so the exit code has to be expanded on the same line -
REM the whole line is parsed before endlocal runs. A separate 'exit /b %GAME_RC%' after
REM endlocal expands to nothing and reports success for a failed run.
endlocal & exit /b %GAME_RC%
