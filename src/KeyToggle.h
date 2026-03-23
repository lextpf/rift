#pragma once

#include <GLFW/glfw3.h>

/**
 * @brief Debounced key-press detector for one-shot toggle actions.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Input
 * @tparam Keys One or more GLFW key codes (int NTTP).
 *
 * Detects the first frame any specified key is pressed, then suppresses
 * repeat detection until all keys are released. Uses variadic NTTP
 * and fold expressions for multi-key support.
 *
 * @par Single Key
 * @code{.cpp}
 * static KeyToggle<GLFW_KEY_E> eKey;
 * if (eKey.JustPressed(window)) { ... }
 * @endcode
 *
 * @par Multi-Key (OR press, AND release)
 * @code{.cpp}
 * static KeyToggle<GLFW_KEY_UP, GLFW_KEY_W> upKey;
 * if (upKey.JustPressed(window)) { ... }
 * @endcode
 */
template <int... Keys>
struct KeyToggle
{
    bool pressed = false;

    /**
     * @brief Returns true on the first frame any key is pressed.
     *
     * Press triggers when any key is down and the toggle is not already pressed.
     * Release resets when all keys are released.
     *
     * @param window GLFW window to query.
     * @return true on the transition from released to pressed.
     */
    bool JustPressed(GLFWwindow* window)
    {
        // Unary left fold (... || expr): expands to (expr(K1) || expr(K2) || ...).
        // True when ANY key in the pack is pressed.
        if ((... || (glfwGetKey(window, Keys) == GLFW_PRESS)) && !pressed)
        {
            pressed = true;
            return true;
        }
        // Unary left fold (... && expr): expands to (expr(K1) && expr(K2) && ...).
        // True only when ALL keys in the pack are released.
        if ((... && (glfwGetKey(window, Keys) == GLFW_RELEASE)))
            pressed = false;
        return false;
    }
};
