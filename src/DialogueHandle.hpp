#pragma once

#include <cstdint>

/// @brief Dialogue-tree id type used as a @ref DialogueStore key.
using DialogueId = std::uint32_t;

/**
 * @struct DialogueHandle
 * @brief A trivially-copyable reference to a @ref DialogueTree owned by a @ref DialogueStore.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Replaces an owned @ref DialogueTree member on an NPC: the store owns the
 * (non-reflectable) node graph, the entity (and, later, an ECS @c Dialogue
 * component) holds only this handle. Flat aggregate (one field, no ctors) so it
 * is usable directly inside a reflectable ECS component. @c id 0 means "no tree".
 */
struct DialogueHandle
{
    DialogueId id = 0;  ///< Store key; 0 = invalid / no tree.
};

inline bool operator==(DialogueHandle a, DialogueHandle b) noexcept
{
    return a.id == b.id;
}
inline bool operator!=(DialogueHandle a, DialogueHandle b) noexcept
{
    return a.id != b.id;
}
