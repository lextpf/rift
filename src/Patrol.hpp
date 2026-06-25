#pragma once

/**
 * @struct Patrol
 * @brief Per-NPC patrol tile cursor (current + target waypoint tiles).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The patrol cursor carved out of NonPlayerCharacter's loose members. The
 * generated waypoint route itself (@ref PatrolRoute) is kept separately as a
 * runtime-regenerable cache - only this small integer cursor is authored /
 * persistable state.
 *
 * Plain data struct: flat aggregate, usable directly as an ECS component
 * (packed storage).
 */
struct Patrol
{
    int tileX{0};        ///< Current tile column.
    int tileY{0};        ///< Current tile row.
    int targetTileX{0};  ///< Next waypoint tile column.
    int targetTileY{0};  ///< Next waypoint tile row.
};
