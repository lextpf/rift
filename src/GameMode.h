#pragma once

#include <cstdint>

/**
 * @enum GameMode
 * @brief Top-level game state for input/update/render dispatch.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Mutually exclusive top-level state. Editor and dialogue modes are
 * orthogonal sub-states only meaningful when @c GameMode::Playing.
 *
 * | Mode    | Update simulates world | Render world | Input source            |
 * |---------|------------------------|--------------|-------------------------|
 * | Title   | No                     | No           | Title menu              |
 * | Playing | Yes                    | Yes          | Player + editor/dialogue|
 * | Paused  | No                     | Yes (frozen) | Pause menu              |
 */
enum class GameMode : uint8_t
{
    Title,
    Playing,
    Paused
};
