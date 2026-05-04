#pragma once

#include "EditorCommand.h"
#include "NonPlayerCharacter.h"
#include "ParticleSystem.h"
#include "Tilemap.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Place tile IDs and rotations on a layer for one or more tiles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Used by single-tile placement, multi-tile region placement, and the tile-
 * place stroke accumulator. Each Entry captures both the old and new
 * (tileId, rotation) for a (tileX, tileY, layer) coord so Apply and Revert
 * are symmetric.
 */
class PlaceTilesCmd : public EditorCommand
{
public:
    struct Entry
    {
        int tileX;
        int tileY;
        std::size_t layer;
        int oldTileId;
        float oldRotation;
        int newTileId;
        float newRotation;
        bool oldFlipX = false;
        bool newFlipX = false;
        bool oldFlipY = false;
        bool newFlipY = false;
    };

    explicit PlaceTilesCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Toggle per-tile collision flags for one or more tiles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class CollisionToggleCmd : public EditorCommand
{
public:
    struct Entry
    {
        int tileX;
        int tileY;
        bool oldCollision;
        bool newCollision;
    };

    explicit CollisionToggleCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-tile elevation values for one or more tiles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class ElevationSetCmd : public EditorCommand
{
public:
    struct Entry
    {
        int tileX;
        int tileY;
        int oldElevation;
        int newElevation;
    };

    explicit ElevationSetCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Place a fully-constructed NPC into the npcs vector.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * The factory work (Load, dialogue tree, name) happens at the call site - the
 * cmd just owns the moved NPC and shuttles it between its own slot and the
 * npcs vector across Apply / Revert. NPC identity for Revert is by tile coord
 * (tile-coord uniqueness is invariant in the editor's NPC click handler).
 *
 * After Apply the NPC lives in the npcs vector and m_Held is empty.
 * After Revert the NPC lives back in m_Held and is gone from the vector.
 */
class PlaceNPCCmd : public EditorCommand
{
public:
    explicit PlaceNPCCmd(NonPlayerCharacter npc);

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    int m_TileX;
    int m_TileY;
    std::optional<NonPlayerCharacter> m_Held;  ///< Holds NPC while command is in "reverted" state.
};

/**
 * @brief Remove the NPC at (tileX, tileY) from the npcs vector.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Inverse of PlaceNPCCmd. After Apply the NPC lives in m_Held and is gone
 * from the vector; after Revert it has been moved back into the vector.
 */
class RemoveNPCCmd : public EditorCommand
{
public:
    RemoveNPCCmd(int tileX, int tileY)
        : m_TileX(tileX),
          m_TileY(tileY)
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    int m_TileX;
    int m_TileY;
    std::optional<NonPlayerCharacter> m_Held;
};

/// @brief Shared entry shape for per-layer per-tile boolean flag mutations
/// (no-projection, y-sort-plus, y-sort-minus). Type aliased into each cmd
/// class so call sites and tests can use the cmd-specific name.
struct LayerFlagEntry
{
    int tileX;
    int tileY;
    std::size_t layer;
    bool oldFlag;
    bool newFlag;
};

/**
 * @brief Set per-layer no-projection flags for one or more (tile, layer) cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Used by B-mode single-tile click, B-mode Shift+flood-fill, and as part of
 * G-mode structure assignment (which also stamps a structureId).
 */
class NoProjectionToggleCmd : public EditorCommand
{
public:
    using Entry = LayerFlagEntry;

    explicit NoProjectionToggleCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-layer Y-sort-plus flags for one or more (tile, layer) cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class YSortPlusToggleCmd : public EditorCommand
{
public:
    using Entry = LayerFlagEntry;

    explicit YSortPlusToggleCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-layer Y-sort-minus flags for one or more (tile, layer) cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class YSortMinusToggleCmd : public EditorCommand
{
public:
    using Entry = LayerFlagEntry;

    explicit YSortMinusToggleCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-tile animation IDs. Handles the dual-write semantics of
 * Tilemap::SetTileAnimation (mutates both animationMap[idx] and tiles[idx]).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * On Apply, SetTileAnimation may stomp tiles[idx] when assigning a non-empty
 * animation. On Revert, we set the animation back first (which also stomps
 * tiles[idx] to the original animation's first-frame), then re-set the tile
 * back to its true pre-Apply value with SetLayerTile.
 */
class SetTileAnimationCmd : public EditorCommand
{
public:
    struct Entry
    {
        int tileX;
        int tileY;
        int layer;
        int oldAnimId;
        int newAnimId;
        int oldTileId;  ///< Captured tile id before SetTileAnimation stomp.
    };

    explicit SetTileAnimationCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-tile structureId for one or more (tile, layer) cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Used by G-mode flood-fill assign and right-click clear. Often emitted with
 * a NoProjectionToggleCmd via CompositeCmd because structure assignment
 * normally also stamps the per-tile no-projection flag.
 */
class SetTileStructureIdsCmd : public EditorCommand
{
public:
    struct Entry
    {
        int tileX;
        int tileY;
        int layer;
        int oldStructId;
        int newStructId;
    };

    explicit SetTileStructureIdsCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Add a no-projection structure (G-mode anchor placement).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Apply: pushes the structure onto Tilemap's vector, captures the assigned id.
 * Revert: removes the structure (LIFO invariant - undo stack ensures our
 * structure is always at the end of the vector).
 */
class AddStructureCmd : public EditorCommand
{
public:
    AddStructureCmd(glm::vec2 leftAnchor, glm::vec2 rightAnchor, std::string name = {})
        : m_LeftAnchor(leftAnchor),
          m_RightAnchor(rightAnchor),
          m_Name(std::move(name))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] int StructureId() const { return m_StructureId; }

private:
    glm::vec2 m_LeftAnchor;
    glm::vec2 m_RightAnchor;
    std::string m_Name;
    int m_StructureId = -1;  ///< Assigned on first Apply.
};

/**
 * @brief Remove a no-projection structure and capture per-tile structureId
 * references so Revert can restore them.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Tilemap::RemoveNoProjectionStructure clears any tile.structureId == id and
 * decrements ids > id. To make undo lossless, we snapshot which tiles
 * referenced the removed structure and re-assign them on Revert (after
 * InsertNoProjectionStructureAt restores the structure at its original id).
 */
class RemoveStructureCmd : public EditorCommand
{
public:
    explicit RemoveStructureCmd(int id)
        : m_Id(id)
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    struct TileRef
    {
        int x;
        int y;
        int layer;
    };

    int m_Id;
    NoProjectionStructure m_Snapshot;
    std::vector<TileRef> m_TileRefs;
    bool m_Captured = false;
};

/**
 * @brief Add a particle zone (J-mode drag-release commit).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class AddParticleZoneCmd : public EditorCommand
{
public:
    explicit AddParticleZoneCmd(ParticleZone zone)
        : m_Zone(std::move(zone))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    ParticleZone m_Zone;
};

/**
 * @brief Remove a particle zone at a specific index, capturing its data so
 * Revert can re-insert at the same index (preserves index-based tracking).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class RemoveParticleZoneCmd : public EditorCommand
{
public:
    explicit RemoveParticleZoneCmd(std::size_t index)
        : m_Index(index)
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    std::size_t m_Index;
    ParticleZone m_Snapshot{};
    bool m_Captured = false;
};

/**
 * @brief Snapshot of one (tile, layer) cell for clipboard / paste operations.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
struct ClipboardCellLayer
{
    int tileId = -1;
    float rotation = 0.0f;
    bool noProjection = false;
    bool flipX = false;
    bool flipY = false;
    int structureId = -1;
    bool ySortPlus = false;
    bool ySortMinus = false;
    int animationMap = -1;
};

/**
 * @brief Snapshot of one tile across all 10 layers + global per-tile fields.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Captures everything `PasteRegionCmd` needs to clone a tile from one
 * map / location to another. NPCs, particle zones, and structures are not
 * captured (cross-map identity is non-trivial; documented in EDITOR.md).
 */
struct ClipboardCell
{
    static constexpr std::size_t LAYER_COUNT = 10;
    ClipboardCellLayer layers[LAYER_COUNT];
    bool collision = false;
    bool navigation = false;
    int elevation = 0;
};

/**
 * @brief Rectangular region of tile snapshots used by Ctrl+C / Ctrl+V.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
struct ClipboardRegion
{
    int width = 0;
    int height = 0;
    std::vector<ClipboardCell> cells;  ///< size = width * height, row-major

    [[nodiscard]] bool Empty() const { return width <= 0 || height <= 0 || cells.empty(); }
};

/// @brief Reflect a region in place around its geometric center.
///
/// Cell positions swap (columns for X-axis, rows for Y-axis), and per-tile
/// each layer's flip flag on the chosen axis is toggled while rotation is
/// negated (rot -> fmod(360 - rot, 360)). The transform is an involution,
/// so applying it twice reproduces the original.
///
/// @param region    Region to mutate in place.
/// @param flipXAxis True for X-reflection (mirror around vertical axis,
///                  toggles flipX), false for Y-reflection (toggles flipY).
void ReflectClipboardRegion(ClipboardRegion& region, bool flipXAxis);

/**
 * @brief Paste a clipboard region at a destination tile, capturing the
 * pre-paste destination state for lossless undo.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class PasteRegionCmd : public EditorCommand
{
public:
    PasteRegionCmd(int destX, int destY, ClipboardRegion source)
        : m_DestX(destX),
          m_DestY(destY),
          m_Source(std::move(source))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    /// @brief Read a single tile (all 10 layers + per-tile fields) into a
    /// ClipboardCell. Public so the editor's Ctrl+C path can snapshot a
    /// region using the same logic this cmd uses for its own dest snapshot.
    [[nodiscard]] static ClipboardCell ReadCellFrom(const Tilemap& tm, int x, int y);

    /// @brief Snapshot a (width x height) region starting at (x, y) into a
    /// ClipboardRegion. Used by Editor's Ctrl+C handler.
    [[nodiscard]] static ClipboardRegion SnapshotRegion(
        const Tilemap& tm, int x, int y, int width, int height);

private:
    int m_DestX;
    int m_DestY;
    ClipboardRegion m_Source;
    ClipboardRegion m_DestSnapshot;
    bool m_Captured = false;

    static void WriteCellInto(Tilemap& tm, int destX, int destY, const ClipboardCell& cell);
};

/**
 * @brief Add an animated tile definition (K-mode Enter on collected frames).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Apply: pushes the AnimatedTile onto Tilemap's vector, captures the assigned id.
 * Revert: pops the last animation (LIFO invariant - any per-tile reference to
 * this animation must have been removed by an earlier-stacked
 * SetTileAnimationCmd::Revert).
 */
class AddAnimatedTileCmd : public EditorCommand
{
public:
    explicit AddAnimatedTileCmd(AnimatedTile anim)
        : m_Anim(std::move(anim))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] int AnimId() const { return m_AnimId; }

private:
    AnimatedTile m_Anim;
    int m_AnimId = -1;
};

/**
 * @brief Group multiple commands into a single atomic undo entry.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Apply runs each child Apply in order; Revert runs each child Revert in
 * reverse order. Used when one user action mutates multiple Tilemap surfaces
 * (e.g., G-mode structure assignment writes structureId AND noProjection per
 * tile).
 */
class CompositeCmd : public EditorCommand
{
public:
    CompositeCmd(std::string label, std::vector<std::unique_ptr<EditorCommand>> children)
        : m_Label(std::move(label)),
          m_Children(std::move(children))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override { return m_Label; }

    [[nodiscard]] std::size_t ChildCount() const { return m_Children.size(); }

private:
    std::string m_Label;
    std::vector<std::unique_ptr<EditorCommand>> m_Children;
};

/**
 * @brief Toggle navigation walkability flags for one or more tiles, with
 * snapshot-and-restore of NPCs displaced by tiles becoming non-walkable.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Apply: flips nav flags, snapshots-and-erases NPCs now on non-walkable
 * tiles, rebuilds patrol routes. Revert: restores nav flags, re-inserts the
 * snapshotted NPCs, rebuilds patrol routes (idempotent).
 *
 * The snapshot is rebuilt fresh on each Apply (Redo), so an interleaving of
 * other commands between Revert and Redo correctly captures the current
 * displaced NPCs rather than referring to stale ones.
 */
class NavigationStrokeCmd : public EditorCommand
{
public:
    struct Entry
    {
        int tileX;
        int tileY;
        bool oldWalkable;
        bool newWalkable;
    };

    explicit NavigationStrokeCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    void Revert(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
    std::vector<NonPlayerCharacter> m_ErasedNPCs;  ///< Held while reverted.
};
