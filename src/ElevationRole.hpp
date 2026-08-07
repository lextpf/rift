#pragma once

#include "EnumTraits.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

/**
 * @brief Whether a layer's artwork at a cell rises to that cell's elevation.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Elevation itself is stored once per cell (`Tilemap::GetElevation`, pixels) and
 * drives collision, the support graph and walkability. This role is stored per
 * layer, and decides only which artwork rises to that height in the 3D path.
 *
 * The split is the whole point. A bridge cell has one elevation, but the water
 * painted on layer 0 beneath the deck must stay on the ground while the deck on
 * layer 2 lifts:
 *
 * @verbatim
 *   one cell, elevation 6
 *
 *   layer 2  [deck]   Raised  -> height 6
 *   layer 0  [water]  Ground  -> height 0
 *   =============================== scene ground plane
 * @endverbatim
 *
 * Lifting per cell instead would carry the water up with the bridge.
 *
 * @par Why a ramp must be marked
 * A cell stores one integer, so it cannot express a slope. In the shipped map
 * every ramp cell holds a CONSTANT height - the bridge reads 0, 2, 4, 6 across
 * its ramp columns - so lifting flat quads by elevation yields a staircase of
 * plateaus, not a ramp. Nothing in the numbers distinguishes "flat surface at 4"
 * from "surface sloping through 4".
 *
 * @see ElevationAxis, Tilemap::RenderWorld3D
 */
enum class ElevationRole : std::uint8_t
{
    Ground = 0,  ///< Sits at height 0 whatever the cell's elevation says.
    Raised,      ///< Sits flat at the cell's elevation (deck, plateau).
    Ramp         ///< Slopes; edge heights derived from same-layer neighbours.
};

/// @brief Number of entries in @ref ElevationRole.
inline constexpr std::size_t ELEVATION_ROLE_COUNT = 3;

/// @brief Reflection for @ref ElevationRole, used by the editor HUD and map I/O.
template <>
struct EnumTraits<ElevationRole> : EnumTraitsBase<ElevationRole, EnumTraits<ElevationRole>>
{
    static constexpr std::size_t Count = ELEVATION_ROLE_COUNT;
    static constexpr std::string_view Names[] = {"Ground", "Raised", "Ramp"};
};

static_assert(std::size(EnumTraits<ElevationRole>::Names) == ELEVATION_ROLE_COUNT,
              "ElevationRole names must stay in step with ELEVATION_ROLE_COUNT");

static_assert(std::to_underlying(ElevationRole::Ground) == 0,
              "Ground must be the zero value: it is the default of every per-tile role array, "
              "so a freshly resized map would otherwise come up as a grid of ramps");

/**
 * @brief The height rule.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Pure functions taking plain ints and enums - no Tilemap, no renderer - so the
 * geometry can be pinned without a graphics context.
 */
namespace elevationRole
{

/// @brief Scene height this layer's artwork occupies at a cell, in world pixels.
inline constexpr float SurfaceHeight(int cellElevation, ElevationRole role)
{
    return (role == ElevationRole::Ground) ? 0.0f : static_cast<float>(cellElevation);
}

/// @brief One neighbouring cell as seen from the same layer.
struct NeighbourSurface
{
    int elevation = 0;                           ///< That cell's elevation, in pixels.
    ElevationRole role = ElevationRole::Ground;  ///< That cell's role ON THIS LAYER.
};

/**
 * @brief Height of the edge a @ref ElevationRole::Ramp cell shares with one neighbour.
 *
 * A ramp snaps to a settled surface and averages with another ramp:
 *
 * @verbatim
 *   x:      39      40      41      42
 *   elev:    0       2       4       6
 *   role: Ground   Ramp    Ramp   Raised
 *   span:    0    0 -> 3  3 -> 6      6
 *                                ^ exact, no seam
 * @endverbatim
 *
 * Snapping rather than averaging at both ends is what makes the run land on the
 * deck exactly; averaging there would leave a one-pixel step at the top of every
 * ramp.
 */
inline constexpr float EdgeHeight(int myElevation, NeighbourSurface neighbour)
{
    return (neighbour.role == ElevationRole::Ramp)
               ? (static_cast<float>(myElevation) + static_cast<float>(neighbour.elevation)) * 0.5f
               : SurfaceHeight(neighbour.elevation, neighbour.role);
}

}  // namespace elevationRole
