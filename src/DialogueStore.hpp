#pragma once

#include "DialogueHandle.hpp"
#include "DialogueTypes.hpp"

#include <cstddef>
#include <unordered_map>

/**
 * @class DialogueStore
 * @brief Owner of NPC @ref DialogueTree graphs, addressed by @ref DialogueHandle.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Centralizes ownership of the branching dialogue trees that were previously
 * owned by-value on each NonPlayerCharacter. Each NPC entity's @c Dialogue
 * component holds a @ref DialogueHandle instead of an owned tree; the store
 * keeps the one copy. This mirrors @ref TextureStore: the heavy,
 * non-field-serializable resource lives behind a handle so the component stays a
 * flat, reflectable aggregate.
 *
 * @par Pointer stability
 * Trees live in a node-based @c std::unordered_map, so a @c const @c DialogueTree&
 * obtained from @ref Get stays valid across later @ref Add calls (only erasure
 * would invalidate it, and the store never erases).
 *
 * @note @ref Add never dedups and the store never erases, so re-assigning an
 * NPC's tree orphans its previous entry. Usage is set-once (map load / editor
 * placement), so growth is bounded by the number of NPCs.
 */
class DialogueStore
{
public:
    /// @brief Take ownership of a tree (moved in); returns its handle.
    DialogueHandle Add(DialogueTree tree);

    /// @brief True if @p handle refers to a stored tree.
    [[nodiscard]] bool IsValid(DialogueHandle handle) const;

    /// @brief True if @p handle refers to a stored, non-empty tree (has nodes).
    [[nodiscard]] bool HasTree(DialogueHandle handle) const;

    /// @brief Resolve @p handle. Invalid handles resolve to a shared empty tree
    /// so callers can read without null checks.
    [[nodiscard]] const DialogueTree& Get(DialogueHandle handle) const;

    /// @brief Number of trees owned.
    [[nodiscard]] std::size_t Count() const { return m_Trees.size(); }

private:
    std::unordered_map<DialogueId, DialogueTree> m_Trees;  ///< The owned trees.
    DialogueId m_NextId = 1;                               ///< Next id to mint (0 = invalid).
};
