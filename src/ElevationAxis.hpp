#pragma once

#include <cstdint>

/**
 * @enum ElevationAxis
 * @brief Direction along which a tile's elevation engages a traversing entity.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Auto-derived from neighbor elevations by Tilemap::GetElevationAxisAt(x, y): the stronger
 * elevation gradient wins; on a tie the longer contiguous elevated span wins; a truly
 * symmetric platform defaults to X.
 *
 * The axis is the engagement rule of the support graph in @ref SurfaceSystem. Crossing a
 * tile boundary changes an entity's support only when the step is pure along that tile's
 * axis - X requires @c moveDx nonzero and @c moveDy zero, Y requires @c moveDy nonzero and
 * @c moveDx zero. A diagonal step is rejected: it crosses the connector instead of walking
 * along it. Movement perpendicular to the axis passes underneath/over without engaging, so
 * the entity keeps whatever support it already had.
 *
 * - **None** : elevation 0 (bare ground). Emitted only for a cell of height 0, so it is
 *              never a valid entry target; leaving an elevated region onto such a cell is
 *              gated by the source cell's axis instead.
 * - **X**    : horizontal-extending elevation (e.g. east-west bridge); engages on pure
 *              east/west movement, ignored on north/south traversal.
 * - **Y**    : vertical-extending elevation (north-south bridge); symmetric.
 *
 * @verbatim
 *   Top-down tile elevations for an east-west (X-axis) bridge; 0 = bare ground.
 *
 *        +---+---+---+---+---+
 *   N    | 0 | 0 | 0 | 0 | 0 |   A north/south traveller has moveDy != 0, so an
 *   ^    +---+---+---+---+---+   X-axis cell never engages: it keeps its Ground
 *   |    | 0 | 4 | 8 | 4 | 0 |   support and walks under the bridge.
 *   v    +---+---+---+---+---+
 *   S    | 0 | 0 | 0 | 0 | 0 |   Every nonzero cell in that row reports axis X.
 *        +---+---+---+---+---+
 *              ramp deck ramp
 *
 *   Side view, west <-> east travel (moveDy == 0, so the axis does engage):
 *
 *     8                _______
 *     4          _____/       \_____
 *     0   ______/                   \______   <- ground, and the under-bridge lane
 *              |     |       |     |
 *              enter climb  descend exit
 *              0->4  4->8    8->4   4->0
 * @endverbatim
 *
 * Each of those four steps must also pass the height gate in
 * @ref CharacterConstants::MAX_STEP_HEIGHT; see @ref SurfaceSystem for which quantity each
 * edge kind compares against.
 *
 * @see Tilemap::GetElevationAxisAt, SurfaceSystem::ResolveMove, SupportState
 */
enum class ElevationAxis : uint8_t
{
    None = 0,
    X = 1,
    Y = 2,
};
