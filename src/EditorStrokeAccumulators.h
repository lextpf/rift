#pragma once

#include "EditorCommands.h"
#include "UndoRedoStack.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @brief Per-tile mutation accumulators for drag-paint commits.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Drag-paint emits mutations across many frames - one per tile touched. To
 * keep the undo stack from filling up with N tiny entries per drag, the
 * accumulator captures the (oldVal) on the first touch of each tile and the
 * final (newVal) on the last touch. On mouse-up Commit() builds a single
 * composite cmd and Push()es it - skipping Execute() because the tilemap was
 * already mutated frame-by-frame during the drag.
 *
 * Lifecycle:
 *   Begin()                 // mouse-down
 *   Touch(...)              // each per-tile mutation during drag
 *   Commit(stack)           // mouse-up: builds cmd, pushes
 *   Drop()                  // mid-drag mode switch: discard without commit
 *
 * Per-tile dedup ensures repeated touches on the same tile within one stroke
 * preserve the original old-value (so undo restores pre-drag state) while
 * tracking the latest new-value (so re-applies after redo land correctly).
 */

namespace
{

/// @brief Pack tile coordinates and a layer index into a stable 64-bit key
/// suitable for unordered_map dedup during a drag stroke.
///
/// Layout (low bits to high):
/// @f[ \mathit{key} = (\mathit{layer} \ll 42) \mid ((y \mathbin{\&} \mathtt{0x1FFFFF}) \ll 21) \mid
/// (x \mathbin{\&} \mathtt{0x1FFFFF}) @f]
///
/// 21 bits per axis cover maps up to 2,097,152 tiles wide or tall - far
/// beyond any conceivable hand-authored map. Layer fits in 4 of the high 22
/// bits (10 layers today; room to grow). Negative coordinates are masked into
/// the same 21-bit window, which is fine because per-stroke dedup only ever
/// compares keys generated from the same coordinate system in the same frame.
///
/// @param x Tile column.
/// @param y Tile row.
/// @param layer Layer index (0-based).
/// @return Stable 64-bit key for unordered_map lookup.
inline std::uint64_t MakeStrokeKey(int x, int y, std::size_t layer)
{
    return (static_cast<std::uint64_t>(layer) << 42) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y) & 0x1FFFFF) << 21) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x) & 0x1FFFFF));
}

}  // namespace

/// @brief Accumulator for tile + rotation paint strokes (default mode L-drag).
struct TilePlaceStrokeAccum
{
    bool active = false;
    std::vector<PlaceTilesCmd::Entry> entries;
    std::unordered_map<std::uint64_t, std::size_t> indexOf;

    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

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

    void Commit(UndoRedoStack& stack)
    {
        if (active && !entries.empty())
            stack.Push(std::make_unique<PlaceTilesCmd>(std::move(entries)));
        active = false;
        entries.clear();
        indexOf.clear();
    }

    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    [[nodiscard]] bool IsActive() const { return active; }
};

/// @brief Accumulator for collision-toggle strokes (default mode R-drag).
struct CollisionStrokeAccum
{
    bool active = false;
    std::vector<CollisionToggleCmd::Entry> entries;
    std::unordered_map<std::uint64_t, std::size_t> indexOf;

    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

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

    void Commit(UndoRedoStack& stack)
    {
        if (active && !entries.empty())
            stack.Push(std::make_unique<CollisionToggleCmd>(std::move(entries)));
        active = false;
        entries.clear();
        indexOf.clear();
    }

    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    [[nodiscard]] bool IsActive() const { return active; }
};

/// @brief Accumulator for elevation paint strokes (H mode L-drag).
struct ElevationStrokeAccum
{
    bool active = false;
    std::vector<ElevationSetCmd::Entry> entries;
    std::unordered_map<std::uint64_t, std::size_t> indexOf;

    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

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

    void Commit(UndoRedoStack& stack)
    {
        if (active && !entries.empty())
            stack.Push(std::make_unique<ElevationSetCmd>(std::move(entries)));
        active = false;
        entries.clear();
        indexOf.clear();
    }

    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    [[nodiscard]] bool IsActive() const { return active; }
};

/// @brief Accumulator for navigation drag strokes (M mode R-drag).
///
/// Navigation strokes commit via Execute (not Push) because the snapshot-and-
/// erase logic for displaced NPCs runs in the cmd's Apply, which must execute
/// once per stroke. The nav-flag SetNavigation calls during the drag mutate
/// in-place; the cmd's Apply re-applies them as no-ops, then handles NPCs.
struct NavigationStrokeAccum
{
    bool active = false;
    std::vector<NavigationStrokeCmd::Entry> entries;
    std::unordered_map<std::uint64_t, std::size_t> indexOf;

    void Begin()
    {
        active = true;
        entries.clear();
        indexOf.clear();
    }

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

    /// Commits via Execute so the cmd's Apply runs (snapshot + NPC erase +
    /// patrol rebuild). The nav SetNavigation calls inside Apply re-apply
    /// the values that were already set during the drag - a harmless no-op.
    void Commit(UndoRedoStack& stack, Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs)
    {
        if (active && !entries.empty())
            stack.Execute(std::make_unique<NavigationStrokeCmd>(std::move(entries)), tilemap, npcs);
        active = false;
        entries.clear();
        indexOf.clear();
    }

    void Drop()
    {
        active = false;
        entries.clear();
        indexOf.clear();
    }

    [[nodiscard]] bool IsActive() const { return active; }
};
