#pragma once

#include "DialogueHandle.hpp"

#include <string>

/**
 * @struct Dialogue
 * @brief NPC conversation data: identity strings plus a handle to the branching tree.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * @ref type is the sprite-path lookup key, @ref name the display name, and @ref text the simple
 * line used when there is no tree.
 *
 * @ref tree is a @ref DialogueHandle into a @ref DialogueStore. A @ref DialogueTree is an
 * unordered_map node graph: not flat, not usefully reflectable for serialization, and expensive
 * to copy with the component. It is therefore kept out of the component - the store holds the
 * tree and the component carries only the handle.
 */
struct Dialogue
{
    std::string type;     ///< NPC type identifier, used as the sprite-path lookup key.
    std::string name;     ///< Display name shown during dialogue.
    std::string text;     ///< Simple dialogue line, used when no tree is attached.
    DialogueHandle tree;  ///< Handle into the DialogueStore; 0 = no tree.
};
