#pragma once

#include <ecs.hpp>

#include <string>
#include <vector>

class Tilemap;

/**
 * @class EditorCommand
 * @brief Abstract base for editor mutations participating in undo/redo.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Concrete commands carry the delta of a single editor action - tile
 * placement, collision toggle, NPC placement, etc. - and apply or revert it
 * against the live Tilemap and the ECS registry that holds the NPCs. Two
 * capture models exist:
 *  - Eager: the constructor receives Entry records that already hold both the
 *    old and the new value (PlaceTilesCmd, CollisionToggleCmd,
 *    ElevationSetCmd, SetTileStancesCmd, SetElevationRolesCmd, the Y-sort
 *    toggles, SetTileAnimationCmd, SetTileStructureIdsCmd,
 *    NavigationStrokeCmd's nav entries).
 *  - Deferred: the command reads its before-state inside Apply and guards
 *    Revert with a capture flag or an assigned id (AddStructureCmd,
 *    RemoveStructureCmd, RemoveNPCCmd, RemoveParticleZoneCmd, PasteRegionCmd,
 *    and NavigationStrokeCmd's displaced-NPC snapshot).
 *
 * A deferred-capture command must be committed with UndoRedoStack::Execute,
 * never with Push: Push does not call Apply, so nothing is captured and the
 * first Revert does nothing.
 *
 * @par Command pattern lifecycle
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef ctor fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef apply fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef revert fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *
 *     A[ctor: eager delta<br/>or capture deferred to Apply]:::ctor
 *     A --> B1[UndoRedoStack.Execute]:::apply
 *     A --> B2[UndoRedoStack.Push<br/>tilemap already mutated]:::apply
 *     B1 --> C[Apply: writes newVal]:::apply
 *     C --> D{User presses Ctrl+Z?}
 *     B2 --> D
 *     D -->|yes| E[Revert: writes oldVal]:::revert
 *     E --> F{User presses Ctrl+Y?}
 *     F -->|yes| C
 * </pre>
 * @endhtmlonly
 *
 * @par Capture rules
 * Commands must capture concrete tile coords / IDs / values, never references
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

    /**
     * @brief Re-apply the mutation.
     *
     * Runs on UndoRedoStack::Execute and on every Redo, so it runs zero times
     * for a command recorded with Push, once for Execute, and once more per
     * Redo. Revert is therefore the first call a Push-recorded command sees.
     * Every Apply and Revert of one command must receive the same tilemap and
     * registry, because commands cache tilemap-relative ids and indices.
     *
     * @param tilemap Live tilemap the mutation is written to.
     * @param npcs    Live NPC store (the ECS registry); NPC-placement commands
     *                spawn/despawn entities through it, tile-only commands
     *                ignore it.
     */
    virtual void Apply(Tilemap& tilemap, ecs::registry& npcs) = 0;

    /**
     * @brief Revert the mutation back to its pre-Apply state. Called on Undo.
     *
     * @param tilemap Live tilemap the old values are written back to.
     * @param npcs    Live NPC store (the ECS registry).
     */
    virtual void Revert(Tilemap& tilemap, ecs::registry& npcs) = 0;

    /// @brief Short human-readable label for status toasts and HUD overlays.
    [[nodiscard]] virtual std::string DebugLabel() const = 0;
};
