#pragma once

#include "EnumTraits.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

/**
 * @brief Whether a tile's artwork lies on the ground or stands up, and how.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * One authored value per map cell per layer, deliberately independent of the
 * per-tile sorting flags. Deriving uprightness from those flags instead - say
 * `noProjection || ySortPlus || ySortMinus` - stands every flat decal an author
 * tagged for sorting on its edge. In the flat path the two y-sort flags change
 * draw order only and never reach a geometry branch, so they carry no claim about
 * geometry, and neither does the 3D path let them.
 *
 * @par The four stances
 * @verbatim
 *   Flat        Prop          Wall            Structure
 *   grass       lantern       fence panel     house facade
 *   path        bush          hedge           bridge railing
 *   water       signpost      low retaining   cliff face
 *
 *   lies on     stands,       stands,         stands, and is one
 *   the         turns to      locked to       tile of a taller body
 *   ground      the camera    the grid        anchored on its base row
 * @endverbatim
 *
 * @par Pole versus surface
 * Billboarding is a cheat that only holds for artwork with no real extent across
 * its pivot. A lantern is effectively a pole: turning it to face the camera
 * spins it in place and it never leaves the cell it was authored on. A fence
 * panel or a facade is a surface with a genuine orientation in the world, and
 * turning it drags its far end through world space - and a run of individually
 * turning cards fans open like venetian blinds. @ref TileStance::Prop and
 * @ref TileStance::Wall are that distinction, authored rather than guessed from
 * whether a tile happened to touch a neighbor.
 *
 * @see tileRole, Tilemap::RenderWorld3D
 */
enum class TileStance : std::uint8_t
{
    Flat = 0,  ///< Ground artwork: lies on the ground plane, drawn depth-free.
    Prop,      ///< Upright pole: one tile tall on its own row, always turns to the camera.
    Wall,      ///< Upright surface: one tile tall on its own row, locked to the grid.
    Structure  ///< Upright surface: one tile of a multi-tile-tall body on its base row.
};

/// @brief Number of entries in @ref TileStance.
inline constexpr std::size_t TILE_STANCE_COUNT = 4;

/// @brief Reflection for @ref TileStance, used by the editor HUD and map I/O.
template <>
struct EnumTraits<TileStance> : EnumTraitsBase<TileStance, EnumTraits<TileStance>>
{
    static constexpr std::size_t Count = TILE_STANCE_COUNT;
    static constexpr std::string_view Names[] = {"Flat", "Prop", "Wall", "Structure"};
};

static_assert(std::size(EnumTraits<TileStance>::Names) == TILE_STANCE_COUNT,
              "TileStance names must stay in step with TILE_STANCE_COUNT");

static_assert(std::to_underlying(TileStance::Flat) == 0,
              "Flat must be the zero value: it is the default of every per-tile stance array, "
              "so a freshly resized map would otherwise stand its whole floor on edge");
