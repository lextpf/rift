#pragma once

#include "DialogueHandle.hpp"

#include <string>

/**
 * @struct Dialogue
 * @brief NPC conversation data: identity strings + a handle to the branching tree.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Groups the NPC's dialogue state carved out of NonPlayerCharacter's loose
 * members. @ref type is the sprite-path lookup key, @ref name the display name,
 * @ref text the simple non-tree fallback line. @ref tree is a @ref DialogueHandle
 * into a @ref DialogueStore: the branching @ref DialogueTree (an unordered_map
 * node graph) is not field-serializable, so it lives in the store and the
 * component carries only the 4-byte handle.
 *
 * Plain data struct: flat aggregate (three @c std::string + a POD handle), usable
 * directly as an ECS component (the three strings serialize; the handle is an id).
 */
struct Dialogue
{
    std::string type;     ///< NPC type identifier (sprite-path lookup key).
    std::string name;     ///< Display name shown during dialogue.
    std::string text;     ///< Simple dialogue text (fallback when no tree).
    DialogueHandle tree;  ///< Handle into the DialogueStore (0 = no tree).
};
