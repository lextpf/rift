#pragma once

#include <cstdint>

/**
 * @enum ElevationAxis
 * @brief Direction along which a tile's elevation engages a traversing entity.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Auto-derived from neighbor elevations by Tilemap::GetElevationAxisAt(x, y).
 * Drives GameCharacter::UpdatePlane: the entity's logical plane only changes
 * when the destination tile's axis matches the movement direction (or the
 * tile has no axis, i.e. ground). Movement perpendicular to the axis passes
 * underneath without engaging.
 *
 * - **None** : ground or non-elevated tile; plane snaps to tile elevation.
 * - **X**    : horizontal-extending elevation (e.g. east-west bridge); engages
 *              on east/west movement, ignored on north/south traversal.
 * - **Y**    : vertical-extending elevation (north-south bridge); symmetric.
 *
 * @see Tilemap::GetElevationAxisAt, GameCharacter::UpdatePlane
 */
enum class ElevationAxis : uint8_t
{
    None = 0,
    X = 1,
    Y = 2,
};
