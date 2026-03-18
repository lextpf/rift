/*  ============================================================================================== *
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
 *  ============================================================================================== *
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
#include <eh.h>
#include <fcntl.h>
#include <io.h>
#include <signal.h>
#include <windows.h>

// Signal-based crash handler for fatal errors.
// Logs the signal number to rift.txt before terminating.
// Handles SIGABRT, SIGTERM, and SIGINT signals.
// @param sig  The signal number that triggered the crash.
void CrashHandler(int sig)
{
    static const char prefix[] = "CRASH HANDLER: Signal ";
    static const char newline[] = "\n";

    int fd = _open("rift.txt", _O_WRONLY | _O_APPEND | _O_CREAT, 0644);
    if (fd != -1)
    {
        _write(fd, prefix, sizeof(prefix) - 1);

        // Convert signal number to characters without library calls.
        char buf[12];
        int pos = 0;
        int val = sig < 0 ? -sig : sig;

        if (sig < 0)
            buf[pos++] = '-';

        // Find leading digit position.
        int divisor = 1;
        while (val / divisor >= 10)
            divisor *= 10;

        while (divisor > 0)
        {
            buf[pos++] = '0' + static_cast<char>(val / divisor);
            val %= divisor;
            divisor /= 10;
        }

        _write(fd, buf, pos);
        _write(fd, newline, sizeof(newline) - 1);
        _close(fd);
    }
    _exit(1);
}

#endif  // _WIN32

int main()
{
#ifdef _WIN32
    signal(SIGABRT, CrashHandler);
    signal(SIGTERM, CrashHandler);
    signal(SIGINT, CrashHandler);

    // Translate Win32 structured exceptions (access violations, stack
    // overflows, division by zero, etc.) into C++ exceptions so they are
    // caught by the try/catch blocks below instead of crashing silently.
    //
    // Only async-signal-safe operations are used here: low-level _open/_write/_close
    // and manual integer-to-string conversion. Heap allocation (std::ofstream,
    // std::string, std::runtime_error) is unsafe during structured exceptions
    // because the heap may be corrupted or the stack nearly exhausted.
    _set_se_translator(
        [](unsigned int code, struct _EXCEPTION_POINTERS* ep)
        {
            (void)ep;

            static const char prefix[] = "SEH EXCEPTION: Code ";
            static const char newline[] = "\n";

            int fd = _open("rift.txt", _O_WRONLY | _O_APPEND | _O_CREAT, 0644);
            if (fd != -1)
            {
                _write(fd, prefix, sizeof(prefix) - 1);

                // Convert exception code to hex without library calls.
                char buf[12] = "0x";
                int pos = 2;
                for (int shift = 28; shift >= 0; shift -= 4)
                {
                    int nibble = (code >> shift) & 0xF;
                    buf[pos++] = "0123456789ABCDEF"[nibble];
                }
                _write(fd, buf, pos);
                _write(fd, newline, sizeof(newline) - 1);
                _close(fd);
            }
            throw std::runtime_error("SEH Exception");
        });
#endif

    // Log startup then close immediately so the file isn't locked during
    // the entire process lifetime. Catch blocks reopen as needed.
    {
        std::ofstream logFile("rift.txt", std::ios::app);
        logFile << "=== Program Starting ===" << std::endl;
    }

#ifdef _WIN32
    if (AllocConsole())
    {
        FILE* pCout;
        FILE* pCin;
        FILE* pCerr;

        freopen_s(&pCout, "CONOUT$", "w", stdout);
        freopen_s(&pCin, "CONIN$", "r", stdin);
        freopen_s(&pCerr, "CONOUT$", "w", stderr);

        std::cout.clear();
        std::cin.clear();
        std::cerr.clear();
    }
#endif

    std::cout << "=== Game Starting ===" << std::endl;
    Game game;
    try
    {
        // Initialize game subsystems (window, renderer, assets)
        if (!game.Initialize())
        {
            std::cerr << "Failed to initialize game" << std::endl;
            std::cerr << "Check rift.txt for details" << std::endl;

            std::ofstream logFile("rift.txt", std::ios::app);
            logFile << "ERROR: Initialize() returned false" << std::endl;

            std::cin.get();
            return -1;
        }

        std::cout << "Game initialized successfully!" << std::endl;

        // Run the main game loop
        try
        {
            // Cap to monitor refresh rate (244 Hz)
            game.SetTargetFps(244.0f);
            game.Run();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception during game loop: " << e.what() << std::endl;
            std::ofstream logFile("rift.txt", std::ios::app);
            logFile << "EXCEPTION in game loop: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception during game loop" << std::endl;
            std::ofstream logFile("rift.txt", std::ios::app);
            logFile << "UNKNOWN EXCEPTION in game loop" << std::endl;
        }

        // Clean shutdown
        game.Shutdown();
        std::cout << "Game shutdown complete" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        std::cerr << "Press Enter to exit..." << std::endl;

        std::ofstream logFile("rift.txt", std::ios::app);
        logFile << "EXCEPTION in main: " << e.what() << std::endl;

        std::cin.get();
        return -1;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in main" << std::endl;
        std::cerr << "Press Enter to exit..." << std::endl;

        std::ofstream logFile("rift.txt", std::ios::app);
        logFile << "UNKNOWN EXCEPTION in main" << std::endl;

        std::cin.get();
        return -1;
    }

    {
        std::ofstream logFile("rift.txt", std::ios::app);
        logFile << "=== Program Exiting Normally ===" << std::endl;
    }

    return 0;
}
