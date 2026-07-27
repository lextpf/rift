#pragma once

/**
 * @struct Patrol
 * @brief Per-NPC patrol tile cursor: the current and target waypoint tiles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The generated waypoint route lives in a separate @ref PatrolRoute component and is a
 * regenerable runtime cache. This small integer cursor is the only authored, persistable state,
 * and it is what a rebuilt route regenerates from.
 *
 * @see PatrolRoute, NpcAiSystem, NavigationRecalc
 */
struct Patrol
{
    /**
     * @brief Tile column the NPC stands on.
     *
     * A bare floor of the feet X (TileMath::TileIndex), with no boundary nudge. Both cursors are
     * recomputed from the Transform each AI frame, so writing them does not move the NPC.
     */
    int tileX{0};
    /**
     * @brief Tile row the NPC stands on.
     *
     * Uses the standing-row convention rather than a bare floor: the feet Y is nudged up by
     * TileMath::STANDING_EPS, so an NPC resting exactly on a row boundary counts as being on the
     * tile above.
     */
    int tileY{0};
    int targetTileX{0};  ///< Next waypoint tile column.
    int targetTileY{0};  ///< Next waypoint tile row.
};
