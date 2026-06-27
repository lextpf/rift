#pragma once

#include <ecs.hpp>

#include <string>
#include <vector>

class Tilemap;

/**
 * @brief Abstract base for editor mutations participating in undo/redo.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Concrete commands capture the (oldVal, newVal) delta of a single editor
 * action - tile placement, collision toggle, NPC placement, etc. - and know
 * how to apply or revert the delta against the live Tilemap and NPC list.
 *
 * @par Command Pattern Lifecycle
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef ctor fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef apply fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef revert fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *
 *     A[ctor captures<br/>oldVal + newVal]:::ctor --> B[UndoRedoStack.Execute<br/>or Push]:::apply
 *     B --> C[Apply: writes newVal]:::apply
 *     C --> D{User presses Ctrl+Z?}
 *     D -->|yes| E[Revert: writes oldVal]:::revert
 *     E --> F{User presses Ctrl+Y?}
 *     F -->|yes| C
 * </pre>
 * @endhtmlonly
 *
 * @par Capture Rules
 * Commands MUST capture concrete tile coords / IDs / values, never references
 * to EditorContext: the context is rebuilt every frame and reference members
 * dangle if held across frames (see Editor.hpp's `EditorContext` warning).
 *
 * @par DebugLabel
 * Used by the editor HUD ("Undo: Place tiles (4)" toast) - keep it short and
 * include any salient count so a quick glance tells the user what reverted.
 *
 * @see UndoRedoStack, EditorCommands.hpp for the concrete catalog
 */
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    /// @brief Re-apply the mutation. Called on initial Execute and on Redo.
    /// @p npcs is the live NPC store (the ECS registry); NPC-placement commands
    /// spawn/despawn entities through it, tile-only commands ignore it.
    virtual void Apply(Tilemap& tilemap, ecs::registry& npcs) = 0;

    /// @brief Revert the mutation back to its pre-Apply state. Called on Undo.
    virtual void Revert(Tilemap& tilemap, ecs::registry& npcs) = 0;

    /// @brief Short human-readable label for status toasts and HUD overlays.
    [[nodiscard]] virtual std::string DebugLabel() const = 0;
};
