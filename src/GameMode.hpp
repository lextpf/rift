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
 * Title mode is not a gameplay simulation, but it still updates and renders
 * a cosmetic backdrop (sky, particles, animated tiles, and PostFX) behind
 * the menu.
 *
 * | Mode    | Gameplay sim | Cosmetic update | World render   | Input source             |
 * |---------|--------------|-----------------|----------------|--------------------------|
 * | Title   | No           | Yes             | Title backdrop | Title menu               |
 * | Playing | Yes          | Yes             | Gameplay world | Player + editor/dialogue |
 * | Paused  | No           | No              | Frozen world   | Pause menu               |
 */
enum class GameMode : uint8_t
{
    Title,
    Playing,
    Paused
};
