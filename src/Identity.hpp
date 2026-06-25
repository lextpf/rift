#pragma once

#include <cstdint>

/**
 * @struct Identity
 * @brief Stable per-session instance id (0 = unassigned).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The stable NPC identity component, carved out of the GameCharacter base.
 * Carried only by NPC entities (the player is referenced by its entity handle).
 * It is NOT made redundant by the @c ecs::entity handle: a handle is invalidated
 * by @c destroy()+create() (the generation is bumped on free), and the editor /
 * navigation undo flow despawns an NPC and re-spawns it from a detached
 * @ref NpcRecord. @c instanceId carried inside that record bridges the gap so
 * dialogue / console / editor references survive a place->undo->redo, which a
 * raw handle cannot. @ref EntityStore::FindById resolves an id to the current
 * live entity. Runtime-only (not serialized).
 *
 * Plain data struct. Flat aggregate so it is usable directly as an ECS
 * component (packed storage).
 */
struct Identity
{
    std::uint64_t instanceId{0};  ///< Stable per-session identity (0 = unassigned)
};
