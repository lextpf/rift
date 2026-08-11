#pragma once

#include "EditorCommands.hpp"
#include "UndoRedoStack.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @brief File-local scope for the stroke-key packer.
 * @author Fable 5 (https://github.com/claude)
 *
 * Deliberately file-local: the key packer below is an implementation detail of the
 * accumulators, not part of this header's surface. The anonymous namespace gives every
 * including translation unit its own internal-linkage copy, so no other MakeStrokeKey can
 * collide with it and nothing outside can depend on the packing.
 *
 * The caveat: every accumulator's inline Touch() calls this, so each including translation
 * unit's copy of those inline members refers to its own MakeStrokeKey. That is tolerable
 * only because the function is pure, stateless and identical everywhere. Do not add state
 * here, and do not reuse the pattern for anything a caller can observe across translation
 * units.
 */
namespace
{

/**
 * @brief Pack tile coordinates and a layer index into a stable 64-bit key
 * suitable for unordered_map dedup during a drag stroke.
 *
 * Layout (low bits to high):
 * @f[ \mathit{key} = (\mathit{layer} \ll 42) \mid ((y \mathbin{\&} \mathtt{0x1FFFFF}) \ll 21) \mid
 * (x \mathbin{\&} \mathtt{0x1FFFFF}) @f]
 * @verbatim
 *   bit 63                 42 41                 21 20                  0
 *        +-------------------+---------------------+---------------------+
 *        |  layer (22 bits)  |  y & 0x1FFFFF (21)  |  x & 0x1FFFFF (21)  |
 *        +-------------------+---------------------+---------------------+
 *          shifted << 42        shifted << 21          no shift
 * @endverbatim
 *
 * 21 bits per axis cover maps up to 2,097,152 tiles wide or tall - far
 * beyond any conceivable hand-authored map. The layer is left unmasked in the top 22
 * bits; the default 10-layer stack needs 4 of them, leaving room to grow. Negative
 * coordinates are masked into the same 21-bit window, which is fine because per-stroke
 * dedup only ever compares keys generated from the same coordinate system in the same
 * frame. TilePlaceStrokeAccum and @ref ElevationStrokeAccum::TouchRole vary the layer;
 * CollisionStrokeAccum, NavigationStrokeAccum and @ref ElevationStrokeAccum::Touch always
 * pass 0, because collision, navigation and elevation height are per-cell, not per-layer.
 *
 * @param x Tile column.
 * @param y Tile row.
 * @param layer Layer index (0-based).
 * @return Stable 64-bit key for unordered_map lookup.
 */
inline std::uint64_t MakeStrokeKey(int x, int y, std::size_t layer)
{
    return (static_cast<std::uint64_t>(layer) << 42) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y) & 0x1FFFFF) << 21) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x) & 0x1FFFFF));
}

}  // namespace

/**
 * @struct TilePlaceStrokeAccum
 * @brief Accumulator for tile + rotation paint strokes (default mode L-drag).
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Editor
 *
 * First of four sibling accumulators that all share the contract below; the others are
 * @ref CollisionStrokeAccum, @ref ElevationStrokeAccum and @ref NavigationStrokeAccum.
 *
 * Drag-paint emits mutations across many frames - one per tile touched. To
 * keep the undo stack from filling up with N tiny entries per drag, the
 * accumulator captures the (oldVal) on the first touch of each tile and the
 * final (newVal) on the last touch. On mouse-up Commit() collapses the whole
 * drag into one undo entry: three of the four Push() it, skipping Execute()
 * because the tilemap was already mutated frame-by-frame during the drag;
 * @ref NavigationStrokeAccum is the exception and Executes (see its block).
 * The entry is one command per payload type, or a @ref CompositeCmd when a
 * single stroke wrote two surfaces (ElevationStrokeAccum's height + role).
 *
 * Lifecycle:
 * @code
 *   Begin()                 // mouse-down
 *   Touch(...)              // each per-tile mutation during drag
 *   Commit(stack)           // mouse-up: builds cmd, pushes
 *   Drop()                  // mid-drag mode switch: discard without commit
 * @endcode
 *
 * Per-tile dedup ensures repeated touches on the same tile within one stroke
 * preserve the original old-value (so undo restores pre-drag state) while
 * tracking the latest new-value (so re-applies after redo land correctly).
 *
 * The four are near-identical but deliberately not one template. Each Touch() takes the
 * payload its mode edits - tile id, rotation and both flips here; a single bool for
 * collision and navigation; an int height plus a separate per-layer ElevationRole pair for
 * elevation. Each builds a different command type, and NavigationStrokeAccum::Commit needs
 * a different signature entirely.
 */
struct TilePlaceStrokeAccum
{
    bool active = false;  ///< True between Begin() and Commit()/Drop(); Touch() no-ops if false.
    std::vector<PlaceTilesCmd::Entry> entries;  ///< One entry per distinct tile, in touch order.
    /// MakeStrokeKey(x, y, layer) -> index into `entries`, so a re-touched tile updates its
    /// existing entry instead of appending a second one. Valid only within one stroke.
    std::unordered_map<std::uint64_t, std::size_t> indexOf;

    /// @brief Start a stroke on mouse-down, discarding anything left from a previous one.
    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

    /**
     * @brief Record one per-tile mutation, capturing old values only on first touch.
     *
     * The caller mutates the tilemap itself; this only accumulates the delta. On a repeat
     * touch of the same (x, y, layer) the old values are kept and only the new values are
     * overwritten, so undo always restores the pre-drag state.
     *
     * @param x        Tile column.
     * @param y        Tile row.
     * @param layer    Layer index (0-based).
     * @param oldId    Tile id before the stroke touched this cell (-1 = empty).
     * @param oldRot   Rotation in degrees before the stroke touched this cell.
     * @param newId    Tile id being written now.
     * @param newRot   Rotation in degrees being written now.
     * @param oldFlipX Horizontal flip before the stroke touched this cell.
     * @param oldFlipY Vertical flip before the stroke touched this cell.
     * @param newFlipX Horizontal flip being written now.
     * @param newFlipY Vertical flip being written now.
     */
    void Touch(int x,
               int y,
               std::size_t layer,
               int oldId,
               float oldRot,
               int newId,
               float newRot,
               bool oldFlipX = false,
               bool oldFlipY = false,
               bool newFlipX = false,
               bool newFlipY = false)
    {
        if (!active)
            return;
        auto key = MakeStrokeKey(x, y, layer);
        auto it = indexOf.find(key);
        if (it == indexOf.end())
        {
            indexOf.emplace(key, entries.size());
            PlaceTilesCmd::Entry e{};
            e.tileX = x;
            e.tileY = y;
            e.layer = layer;
            e.oldTileId = oldId;
            e.oldRotation = oldRot;
            e.newTileId = newId;
            e.newRotation = newRot;
            e.oldFlipX = oldFlipX;
            e.newFlipX = newFlipX;
            e.oldFlipY = oldFlipY;
            e.newFlipY = newFlipY;
            entries.push_back(e);
        }
        else
        {
            entries[it->second].newTileId = newId;
            entries[it->second].newRotation = newRot;
            entries[it->second].newFlipX = newFlipX;
            entries[it->second].newFlipY = newFlipY;
        }
    }

    /**
     * @brief Push the accumulated delta as one PlaceTilesCmd and end the stroke.
     *
     * Push, not Execute: the tilemap already holds the new values from the per-frame
     * writes during the drag. An empty stroke pushes nothing. Always leaves the
     * accumulator inactive and empty, so calling it twice is harmless.
     */
    void Commit(UndoRedoStack& stack)
    {
        if (active && !entries.empty())
            stack.Push(std::make_unique<PlaceTilesCmd>(std::move(entries)));
        active = false;
        entries.clear();
        indexOf.clear();
    }

    /// @brief Abandon the stroke without pushing. Tiles already painted during the drag
    /// stay painted, but they become non-undoable.
    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    /// @brief Whether a stroke is in progress (Begin() called, not yet committed/dropped).
    [[nodiscard]] bool IsActive() const { return active; }
};

/**
 * @struct CollisionStrokeAccum
 * @brief Accumulator for collision-toggle strokes (default mode R-drag).
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Editor
 *
 * Same Begin/Touch/Commit/Drop contract as @ref TilePlaceStrokeAccum; see there. Collision
 * is a per-cell grid rather than per-layer, so the dedup key always uses layer 0.
 */
struct CollisionStrokeAccum
{
    bool active = false;  ///< True between Begin() and Commit()/Drop(); Touch() no-ops if false.
    std::vector<CollisionToggleCmd::Entry> entries;          ///< One entry per distinct cell.
    std::unordered_map<std::uint64_t, std::size_t> indexOf;  ///< Stroke key -> index in `entries`.

    /// @brief Start a stroke on mouse-down, discarding anything left from a previous one.
    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

    /**
     * @brief Record one cell's collision change; `oldHas` is kept from the first touch.
     * @param x      Tile column.
     * @param y      Tile row.
     * @param oldHas Collision flag before the stroke touched this cell.
     * @param newHas Collision flag being written now.
     */
    void Touch(int x, int y, bool oldHas, bool newHas)
    {
        if (!active)
            return;
        auto key = MakeStrokeKey(x, y, 0);
        auto it = indexOf.find(key);
        if (it == indexOf.end())
        {
            indexOf.emplace(key, entries.size());
            entries.push_back(CollisionToggleCmd::Entry{x, y, oldHas, newHas});
        }
        else
        {
            entries[it->second].newCollision = newHas;
        }
    }

    /// @brief Push the accumulated delta as one CollisionToggleCmd and end the stroke.
    /// Push, not Execute - the drag already wrote the values. Empty strokes push nothing.
    void Commit(UndoRedoStack& stack)
    {
        if (active && !entries.empty())
            stack.Push(std::make_unique<CollisionToggleCmd>(std::move(entries)));
        active = false;
        entries.clear();
        indexOf.clear();
    }

    /// @brief Abandon the stroke without pushing; already-written flags stay, un-undoable.
    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    /// @brief Whether a stroke is in progress (Begin() called, not yet committed/dropped).
    [[nodiscard]] bool IsActive() const { return active; }
};

/**
 * @struct ElevationStrokeAccum
 * @brief Accumulator for elevation paint strokes (H mode L-drag).
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Editor
 *
 * Same Begin/Touch/Commit/Drop contract as @ref TilePlaceStrokeAccum; see there, with one
 * extra wrinkle: two dedup schemes coexist. @c Touch's height key always uses layer 0, because
 * elevation is per-cell; @c TouchRole's role key includes the layer, because the role is
 * per-layer. Each keys into its own map (`indexOf` / `roleIndexOf`), so the two schemes cannot
 * collide even though they may carry different layer values for the same cell.
 */
struct ElevationStrokeAccum
{
    bool active = false;  ///< True between Begin() and Commit()/Drop(); Touch() no-ops if false.
    std::vector<ElevationSetCmd::Entry> entries;             ///< One entry per distinct cell.
    std::unordered_map<std::uint64_t, std::size_t> indexOf;  ///< Stroke key -> index in `entries`.
    std::vector<LayerElevationRoleEntry> roleEntries;  ///< One entry per distinct (cell, layer).
    std::unordered_map<std::uint64_t, std::size_t>
        roleIndexOf;  ///< Stroke key -> index in `roleEntries`.

    /// @brief Start a stroke on mouse-down, discarding anything left from a previous one.
    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
        roleEntries.clear();
        roleIndexOf.clear();
    }

    /**
     * @brief Record one cell's elevation change; `oldElev` is kept from the first touch.
     * @param x       Tile column.
     * @param y       Tile row.
     * @param oldElev Elevation in pixels before the stroke touched this cell (0 = ground).
     * @param newElev Elevation in pixels being written now.
     */
    void Touch(int x, int y, int oldElev, int newElev)
    {
        if (!active)
            return;
        auto key = MakeStrokeKey(x, y, 0);
        auto it = indexOf.find(key);
        if (it == indexOf.end())
        {
            indexOf.emplace(key, entries.size());
            entries.push_back(ElevationSetCmd::Entry{x, y, oldElev, newElev});
        }
        else
        {
            entries[it->second].newElevation = newElev;
        }
    }

    /**
     * @brief Record one cell's elevation-role change on one layer.
     *
     * Separate from @ref Touch because the role is per-layer while the height is
     * per-cell, so the two use different dedup keys and land in different commands.
     *
     * @param x       Tile column.
     * @param y       Tile row.
     * @param layer   Layer index (0-based).
     * @param oldRole Role before the stroke first touched this cell.
     * @param newRole Role being written now.
     */
    void TouchRole(int x, int y, std::size_t layer, ElevationRole oldRole, ElevationRole newRole)
    {
        if (!active)
            return;
        auto key = MakeStrokeKey(x, y, layer);
        auto it = roleIndexOf.find(key);
        if (it == roleIndexOf.end())
        {
            roleIndexOf.emplace(key, roleEntries.size());
            roleEntries.push_back(LayerElevationRoleEntry{x, y, layer, oldRole, newRole});
        }
        else
        {
            roleEntries[it->second].newRole = newRole;
        }
    }

    /// @brief Push the accumulated delta and end the stroke. Height and role commit
    /// together as one composite so a single Ctrl+Z undoes both halves of one click.
    /// Push, not Execute - the drag already wrote the values. Empty strokes push nothing.
    void Commit(UndoRedoStack& stack)
    {
        if (active)
        {
            const bool haveHeights = !entries.empty();
            const bool haveRoles = !roleEntries.empty();
            if (haveHeights && haveRoles)
            {
                std::vector<std::unique_ptr<EditorCommand>> children;
                children.push_back(std::make_unique<ElevationSetCmd>(std::move(entries)));
                children.push_back(std::make_unique<SetElevationRolesCmd>(std::move(roleEntries)));
                stack.Push(std::make_unique<CompositeCmd>("Paint elevation", std::move(children)));
            }
            else if (haveHeights)
            {
                stack.Push(std::make_unique<ElevationSetCmd>(std::move(entries)));
            }
            else if (haveRoles)
            {
                stack.Push(std::make_unique<SetElevationRolesCmd>(std::move(roleEntries)));
            }
        }
        Drop();
    }

    /// @brief Abandon the stroke without pushing; already-written values stay, un-undoable.
    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
        roleEntries.clear();
        roleIndexOf.clear();
    }

    /// @brief Whether a stroke is in progress (Begin() called, not yet committed/dropped).
    [[nodiscard]] bool IsActive() const { return active; }
};

/**
 * @struct NavigationStrokeAccum
 * @brief Accumulator for navigation drag strokes (M mode R-drag).
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Editor
 *
 * Begin/Touch/Drop match @ref TilePlaceStrokeAccum, but Commit is the odd one out: this is
 * the only accumulator that commits via Execute rather than Push, because the
 * snapshot-and-erase logic for displaced NPCs lives in the cmd's Apply and must run once
 * per stroke. The nav-flag SetNavigation calls during the drag mutate in-place; the cmd's
 * Apply re-applies them as no-ops, then handles NPCs. Its Commit therefore also needs the
 * tilemap and the registry, which the other three do not.
 */
struct NavigationStrokeAccum
{
    bool active = false;  ///< True between Begin() and Commit()/Drop(); Touch() no-ops if false.
    std::vector<NavigationStrokeCmd::Entry> entries;         ///< One entry per distinct cell.
    std::unordered_map<std::uint64_t, std::size_t> indexOf;  ///< Stroke key -> index in `entries`.

    /// @brief Start a stroke on mouse-down, discarding anything left from a previous one.
    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

    /**
     * @brief Record one cell's walkability change; `oldWalk` is kept from the first touch.
     * @param x       Tile column.
     * @param y       Tile row.
     * @param oldWalk Walkability before the stroke touched this cell.
     * @param newWalk Walkability being written now.
     */
    void Touch(int x, int y, bool oldWalk, bool newWalk)
    {
        if (!active)
            return;
        auto key = MakeStrokeKey(x, y, 0);
        auto it = indexOf.find(key);
        if (it == indexOf.end())
        {
            indexOf.emplace(key, entries.size());
            entries.push_back(NavigationStrokeCmd::Entry{x, y, oldWalk, newWalk});
        }
        else
        {
            entries[it->second].newWalkable = newWalk;
        }
    }

    /**
     * @brief Commit via Execute so the cmd's Apply runs (snapshot + NPC erase +
     * patrol rebuild).
     *
     * The SetNavigation calls inside Apply re-apply the values that were already set
     * during the drag - a harmless no-op. Empty strokes commit nothing.
     *
     * @param stack   Undo history the command is executed through.
     * @param tilemap Live tilemap the command re-applies nav flags to.
     * @param npcs    Live registry the command erases displaced NPCs from.
     */
    void Commit(UndoRedoStack& stack, Tilemap& tilemap, ecs::registry& npcs)
    {
        if (active && !entries.empty())
            stack.Execute(std::make_unique<NavigationStrokeCmd>(std::move(entries)), tilemap, npcs);
        active = false;
        entries.clear();
        indexOf.clear();
    }

    /// @brief Abandon the stroke without committing. Nav flags already written during the
    /// drag stay, but no NPC snapshot/erase pass runs and nothing lands on the undo stack.
    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    /// @brief Whether a stroke is in progress (Begin() called, not yet committed/dropped).
    [[nodiscard]] bool IsActive() const { return active; }
};
