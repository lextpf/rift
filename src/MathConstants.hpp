#pragma once

/**
 * @brief Engine-wide mathematical constants in the root @c rift namespace.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Header-only `inline constexpr` values, so every translation unit shares one
 * definition and nothing is initialized at run time.
 */

namespace rift
{
inline constexpr double Pi = 3.14159265358979323846;  ///< Pi (double precision).
inline constexpr float PiF = 3.14159265f;  ///< Pi (single precision, ~7 significant digits).
}  // namespace rift
