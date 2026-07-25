#pragma once

#include <cstdint>

/**
 * @struct Identity
 * @brief Stable per-session instance id, where 0 means unassigned.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Carried only by NPC entities. The player is referenced by its entity handle instead.
 *
 * An @c ecs::entity handle cannot do this job, because destroying and recreating an entity bumps
 * the generation and invalidates the existing handle. The editor and navigation undo flow despawns
 * an NPC and respawns it from a detached @ref NpcRecord, and the id travels inside that record.
 * That is what lets dialogue, console and editor references survive a place, undo and redo cycle.
 * @ref EntityStore::FindById resolves an id back to the live entity.
 *
 * Runtime-only, never serialized.
 */
struct Identity
{
    std::uint64_t instanceId{0};  ///< Stable per-session identity; 0 = unassigned.
};
