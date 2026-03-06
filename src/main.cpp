/*  ==============================================================================================  *
 *                                                                                                
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠳⣶⡤
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠠⣾⣦⡀
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣈⣻⡧⢀
 *           :::::::::  ::::::::::: :::::::::: :::::::::::    ⢷⣦⣤⡀⠀⢀⣠⣤⡆⢰⣶⣶⣾⣿⣿⣷⣕⣡⡀
 *           :+:    :+:     :+:     :+:            :+:        ⠘⣿⣿⠇⠀⣦⡀⠉⠉⠈⠉⠁⢸⣿⣿⣿⣿⡿⠃
 *           +:+    +:+     +:+     +:+            +:+        ⠀⠀⠀⣀⣴⣿⣿⣄⣀⣀⣀⢀⣼⣿⣿⣿⠁
 *           +#++:++#:      +#+     :#::+::#       +#+        ⠀⠀⠀⠀⠉⢩⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡀
 *           +#+    +#+     +#+     +#+            +#+        ⠀⠀⠀⠀⠀⣸⣿⣿⡿⢻⣿⣿⣿⣿⡿⢿⠇
 *           #+#    #+#     #+#     #+#            #+#        ⠀⠀⠀⠀⢰⣿⣿⣿⠰⠙⠁⠈⣿⣿⠱⠘
 *           ###    ### ########### ###            ###        ⠀⠀⠀⠀⢸⡏⣾⡿⠁⠀⠀⠀⢿⣼⣷⠁
 *                                                            ⠀⠀⠀⠀⠘⠷⢿⣧⡀⠀⠀⠀⠈⠛⢿⣆
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠉⠉⠀⠀⠀⠀⠀⠀⠈
 *                                  << G A M E   E N G I N E >>                        
 *                                                                                                  
 *  ==============================================================================================  *
 * 
 *      A 2.5D game engine featuring dual graphics backends (OpenGL 4.6 &
 *      Vulkan 1.0), dynamic day/night cycles, tile-based worlds, NPC
 *      pathfinding, and a built-in level editor.
 *
 *    ----------------------------------------------------------------------
 *
 *      Repository:   https://github.com/lextpf/rift
 *      License:      MIT
 */
#include "Game.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <signal.h>
#include <eh.h>

/**
 * @brief Signal-based crash handler for fatal errors.
 *
 * Logs the signal number to rift.txt before terminating.
 * Handles SIGABRT, SIGTERM, and SIGINT signals.
 *
 * @param sig The signal number that triggered the crash.
 */
void CrashHandler(int sig)
{
    std::ofstream logFile("rift.txt", std::ios::app);
    logFile << "CRASH HANDLER: Signal " << sig << std::endl;
    logFile.flush();
    logFile.close();
    exit(1);
}

#endif // _WIN32

int main()
{
    // ------------------------------------------------------------------------
    // Windows: Install Crash Handlers
    // ------------------------------------------------------------------------
#ifdef _WIN32
    signal(SIGABRT, CrashHandler);
    signal(SIGTERM, CrashHandler);
    signal(SIGINT, CrashHandler);

    // Translate structured exceptions (SEH) to C++ exceptions
    _set_se_translator([](unsigned int code, struct _EXCEPTION_POINTERS *ep)
                       {
        (void)ep; // Unused parameter

        std::ofstream logFile("rift.txt", std::ios::app);
        logFile << "SEH EXCEPTION: Code " << code << std::endl;
        logFile.flush();
        logFile.close();
        throw std::runtime_error("SEH Exception"); });
#endif

    // ------------------------------------------------------------------------
    // Initialize Logging
    // ------------------------------------------------------------------------
    std::ofstream logFile("rift.txt", std::ios::app);
    logFile << "=== Program Starting ===" << std::endl;

    // ------------------------------------------------------------------------
    // Windows: Allocate Debug Console
    // ------------------------------------------------------------------------
#ifdef _WIN32
    if (AllocConsole())
    {
        FILE *pCout;
        FILE *pCin;
        FILE *pCerr;

        freopen_s(&pCout, "CONOUT$", "w", stdout);
        freopen_s(&pCin, "CONIN$", "r", stdin);
        freopen_s(&pCerr, "CONOUT$", "w", stderr);

        std::cout.clear();
        std::cin.clear();
        std::cerr.clear();
    }
#endif

    std::cout << "=== Game Starting ===" << std::endl;

    // ------------------------------------------------------------------------
    // Game Initialization and Execution
    // ------------------------------------------------------------------------
    Game game;

    try
    {
        // Initialize game subsystems (window, renderer, assets)
        if (!game.Initialize())
        {
            std::cerr << "Failed to initialize game" << std::endl;
            std::cerr << "Check rift.txt for details" << std::endl;

            logFile << "ERROR: Initialize() returned false" << std::endl;
            logFile.close();

            std::cin.get();
            return -1;
        }

        std::cout << "Game initialized successfully!" << std::endl;

        // Run the main game loop
        try
        {
            game.SetTargetFps(500.0f);
            game.Run();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception during game loop: " << e.what() << std::endl;
            logFile << "EXCEPTION in game loop: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception during game loop" << std::endl;
            logFile << "UNKNOWN EXCEPTION in game loop" << std::endl;
        }

        // Clean shutdown
        game.Shutdown();
        std::cout << "Game shutdown complete" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        std::cerr << "Press Enter to exit..." << std::endl;

        logFile << "EXCEPTION in main: " << e.what() << std::endl;
        logFile.close();

        std::cin.get();
        return -1;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in main" << std::endl;
        std::cerr << "Press Enter to exit..." << std::endl;

        logFile << "UNKNOWN EXCEPTION in main" << std::endl;
        logFile.close();

        std::cin.get();
        return -1;
    }

    // ------------------------------------------------------------------------
    // Clean Exit
    // ------------------------------------------------------------------------
    logFile << "=== Program Exiting Normally ===" << std::endl;
    logFile.close();

    return 0;
}
