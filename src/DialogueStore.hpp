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
 * Centralizes ownership of the branching dialogue trees: an NPC entity's
 * @c Dialogue component holds a @ref DialogueHandle rather than an owned tree, and
 * the store keeps the one copy. This mirrors @ref TextureStore - the heavy,
 * non-field-serializable resource lives behind a handle so the component stays a
 * flat, reflectable aggregate.
 *
 * @par Ownership chain
 * Nothing but this store owns a tree. @ref DialogueManager takes a private COPY at
 * @c StartDialogue so its node/option pointers survive the NPC being despawned
 * mid-conversation:
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     NPC["NPC entity"] --> Comp["Dialogue component"]
 *     Comp --> H["DialogueHandle .id"]
 *     H -- "map key" --> Map["DialogueStore::m_Trees"]
 *     Map --> Tree["DialogueTree (the one owner)"]
 *     Tree -- "copied at StartDialogue" --> Active["DialogueManager::m_ActiveTree"]
 * </pre>
 * @endhtmlonly
 *
 * @par Pointer stability
 * Trees live in a node-based @c std::unordered_map, so a @c const @c DialogueTree&
 * obtained from @ref Get stays valid across later @ref Add calls (only erasure
 * would invalidate it, and the store never erases).
 *
 * @note The store is append-only for the whole process lifetime: there is no erase
 * or clear API, @ref Add never dedups, and nothing clears the store when NPC
 * entities are destroyed. A map load and each editor redo of an NPC placement
 * therefore mint fresh trees on top of the orphaned previous ones, so the store
 * grows across reloads and editor sessions.
 *
 * @note A handle is never invalidated. A handle held past its NPC's despawn keeps
 * resolving to the orphaned tree instead of reporting invalid, and a handle is only
 * meaningful for the store instance that minted it.
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
