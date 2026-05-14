#pragma once

#include <string>
#include <vector>

class Tilemap;
class NonPlayerCharacter;

/**
 * @brief Abstract base for editor mutations participating in undo/redo.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Concrete commands capture the (oldVal, newVal) delta of a single editor
 * action - tile placement, collision toggle, NPC placement, etc. - and know
 * how to apply or revert the delta against the live Tilemap and NPC list.
 *
 * Commands MUST capture concrete tile coords / IDs / values, never references
 * to EditorContext: the context is rebuilt every frame and reference members
 * dangle if held across frames (see Editor.h:41-43).
 */
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    /// @brief Re-apply the mutation. Called on initial Execute and on Redo.
    virtual void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) = 0;

    /// @brief Revert the mutation back to its pre-Apply state. Called on Undo.
    virtual void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) = 0;

    /// @brief Short human-readable label for status toasts and HUD overlays.
    [[nodiscard]] virtual std::string DebugLabel() const = 0;
};
