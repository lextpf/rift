#pragma once

#include "EditorCommand.hpp"
#include "ElevationRole.hpp"
#include "NpcRecord.hpp"
#include "ParticleSystem.hpp"
#include "Tilemap.hpp"

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
 * (tileId, rotation, flipX, flipY) for a (tileX, tileY, layer) coord so
 * Apply and Revert are symmetric. This is the canonical Entry shape the rest of the
 * header follows.
 *
 * @par Catalog of the EditorCommand subclasses declared in this header
 * Each class represents one user-visible editor action that can be undone/redone via
 * UndoRedoStack. They fall into a few families:
 *
 * | Family               | Commands                                                |
 * |----------------------|---------------------------------------------------------|
 * | Tile painting        | `PlaceTilesCmd`, `PasteRegionCmd`                       |
 * | Per-tile flags       | `CollisionToggleCmd`, `SetTileStancesCmd`,              |
 * |                      | `YSortPlusToggleCmd`, `YSortMinusToggleCmd`             |
 * | Elevation            | `ElevationSetCmd`, `SetElevationRolesCmd`               |
 * | NPC lifecycle        | `PlaceNPCCmd`, `RemoveNPCCmd`                           |
 * | Structures           | `AddStructureCmd`, `RemoveStructureCmd`,                |
 * |                      | `SetTileStructureIdsCmd`                                |
 * | Particle zones       | `AddParticleZoneCmd`, `RemoveParticleZoneCmd`           |
 * | Animation            | `AddAnimatedTileCmd`, `SetTileAnimationCmd`             |
 * | Navigation           | `NavigationStrokeCmd` (snapshots displaced NPCs)        |
 * | Composition          | `CompositeCmd` (atomic multi-command grouping)          |
 *
 * Most "Entry" structs follow the same shape: `(coords, oldValue, newValue)`
 * so Apply/Revert are symmetric. The few with side effects (NPC erase, tile
 * stomping during animation set, structure ID re-stamping) document their
 * subtleties in the per-class docblocks.
 *
 * @see EditorCommand for the base interface and command-pattern lifecycle.
 */
class PlaceTilesCmd : public EditorCommand
{
public:
    /// One (tile, layer) cell's before/after state. Coordinates are tile indices; entries
    /// for out-of-range cells are harmless because the Tilemap setters bounds-check.
    struct Entry
    {
        int tileX;              ///< Tile X coordinate.
        int tileY;              ///< Tile Y coordinate.
        std::size_t layer;      ///< Dynamic layer index.
        int oldTileId;          ///< Tile ID before Apply().
        float oldRotation;      ///< Rotation before Apply(), in degrees.
        int newTileId;          ///< Tile ID after Apply().
        float newRotation;      ///< Rotation after Apply(), in degrees.
        bool oldFlipX = false;  ///< Horizontal flip before Apply().
        bool newFlipX = false;  ///< Horizontal flip after Apply().
        bool oldFlipY = false;  ///< Vertical flip before Apply().
        bool newFlipY = false;  ///< Vertical flip after Apply().
    };

    explicit PlaceTilesCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-cell collision flags for one or more cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Collision is a single per-cell grid on the Tilemap, not a per-layer field, so entries
 * carry no layer index. Despite the name the command sets an explicit value rather than
 * flipping the current one, which is what makes Apply/Revert symmetric.
 */
class CollisionToggleCmd : public EditorCommand
{
public:
    /// One cell's before/after collision state.
    struct Entry
    {
        int tileX;          ///< Tile column.
        int tileY;          ///< Tile row.
        bool oldCollision;  ///< Blocking state before Apply(); restored by Revert().
        bool newCollision;  ///< Blocking state written by Apply(); true = blocks movement.
    };

    explicit CollisionToggleCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Set per-cell elevation values for one or more cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Elevation is a single per-cell grid on the Tilemap, so entries carry no layer index.
 */
class ElevationSetCmd : public EditorCommand
{
public:
    /// One cell's before/after elevation, in pixels (0 = ground level, positive = higher).
    struct Entry
    {
        int tileX;         ///< Tile column.
        int tileY;         ///< Tile row.
        int oldElevation;  ///< Elevation in pixels before Apply(); restored by Revert().
        int newElevation;  ///< Elevation in pixels written by Apply().
    };

    explicit ElevationSetCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Spawn an authored NpcRecord blueprint into the NPC registry.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * The call site builds the record (type, name, dialogue text, tile); Apply
 * resolves the sprite sheet and the dialogue tree through
 * EntityStore::SpawnNpc, which reads the services in @c registry.globals().
 * NPC identity for Revert is by tile coord (tile-coord uniqueness is invariant
 * in the editor's NPC click handler): Revert re-snapshots whatever entity
 * stands on that tile and destroys it, so component state outside NpcRecord
 * does not survive an undo/redo cycle. Revert does nothing when no entity with
 * @ref Patrol and @ref NpcTag sits on the tile any more.
 *
 * After Apply the NPC lives in the registry and m_Held is empty.
 * After Revert the NPC lives back in m_Held and is gone from the registry.
 */
class PlaceNPCCmd : public EditorCommand
{
public:
    explicit PlaceNPCCmd(NpcRecord npc);

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    int m_TileX;
    int m_TileY;
    std::optional<NpcRecord> m_Held;  ///< Holds NPC while command is in "reverted" state.
};

/**
 * @brief Remove the NPC at (tileX, tileY) from the NPC registry.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Inverse of PlaceNPCCmd. After Apply the NPC lives in m_Held as a detached
 * NpcRecord and is gone from the registry; Revert re-spawns it through
 * EntityStore::SpawnNpc. Apply does nothing when no NPC stands on the tile,
 * which leaves m_Held empty and makes Revert a no-op as well.
 */
class RemoveNPCCmd : public EditorCommand
{
public:
    RemoveNPCCmd(int tileX, int tileY)
        : m_TileX(tileX),
          m_TileY(tileY)
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

private:
    int m_TileX;
    int m_TileY;
    std::optional<NpcRecord> m_Held;
};

/**
 * @brief Shared entry shape for per-layer per-tile boolean flag mutations
 * (y-sort-plus, y-sort-minus).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Type-aliased into each cmd class so call sites and tests can use the cmd-specific name.
 * Which flag an entry refers to is decided by the owning command, not by the entry.
 */
struct LayerFlagEntry
{
    int tileX;          ///< Tile column.
    int tileY;          ///< Tile row.
    std::size_t layer;  ///< Layer index (0-based); these flags are per-layer, not per-cell.
    bool oldFlag;       ///< Flag value before Apply(); restored by Revert().
    bool newFlag;       ///< Flag value written by Apply().
};

/**
 * @brief Entry shape for per-layer per-tile @ref TileStance mutations.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Deliberately not a @ref LayerFlagEntry - stance is a four-valued enum, and a bool
 * pair cannot carry four values without losing information.
 */
struct LayerStanceEntry
{
    int tileX;             ///< Tile column.
    int tileY;             ///< Tile row.
    std::size_t layer;     ///< Layer index (0-based); stance is per-layer, not per-cell.
    TileStance oldStance;  ///< Stance before Apply(); restored by Revert().
    TileStance newStance;  ///< Stance written by Apply().
};

/**
 * @brief Set per-layer tile stances for one or more (tile, layer) cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Used by B-mode single-tile click, B-mode Shift+flood-fill, and as part of
 * G-mode structure assignment (which also stamps a structureId).
 */
class SetTileStancesCmd : public EditorCommand
{
public:
    using Entry = LayerStanceEntry;

    explicit SetTileStancesCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
};

/**
 * @brief Entry shape for per-layer per-tile @ref ElevationRole mutations.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Carries a layer index because the role is per-layer, unlike
 * @ref ElevationSetCmd::Entry which is per-cell. H mode writes both and bundles
 * them in a @ref CompositeCmd rather than merging them into one entry type.
 */
struct LayerElevationRoleEntry
{
    int tileX;              ///< Tile column.
    int tileY;              ///< Tile row.
    std::size_t layer;      ///< Layer index (0-based).
    ElevationRole oldRole;  ///< Role before Apply(); restored by Revert().
    ElevationRole newRole;  ///< Role written by Apply().
};

/**
 * @brief Set per-layer elevation roles for one or more (tile, layer) cells.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 */
class SetElevationRolesCmd : public EditorCommand
{
public:
    using Entry = LayerElevationRoleEntry;

    explicit SetElevationRolesCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 * On Apply, SetTileAnimation stomps tiles[idx] with the animation's first
 * frame when the new id names a non-empty animation. Revert restores the old
 * animation id first, which leaves tiles[idx] wrong either way: restoring a
 * real animation stomps it to that animation's first frame, and restoring -1
 * leaves whatever the applied animation stamped. Revert therefore rewrites the
 * captured Entry::oldTileId with SetLayerTile, which is the only reason that
 * field exists.
 */
class SetTileAnimationCmd : public EditorCommand
{
public:
    /// One (tile, layer) cell's before/after animation assignment.
    struct Entry
    {
        int tileX;      ///< Tile column.
        int tileY;      ///< Tile row.
        int layer;      ///< Layer index (0-based); animation ids are per-layer.
        int oldAnimId;  ///< Animation id before Apply() (-1 = not animated).
        int newAnimId;  ///< Animation id written by Apply() (-1 clears the animation).
        int oldTileId;  ///< Captured tile id before SetTileAnimation stomp.
    };

    explicit SetTileAnimationCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 * a SetTileStancesCmd via CompositeCmd because structure assignment
 * normally also stamps TileStance::Structure onto the tiles.
 */
class SetTileStructureIdsCmd : public EditorCommand
{
public:
    /// One (tile, layer) cell's before/after structure assignment.
    struct Entry
    {
        int tileX;  ///< Tile column.
        int tileY;  ///< Tile row.
        /**
         * @brief Layer index in the 1-BASED convention of Tilemap::Get/SetTileStructureId,
         * which this command passes through unchanged.
         *
         * The editor call sites build entries with `m_CurrentLayer + 1`; passing a 0-based
         * index here silently targets the layer below (and index 0 is rejected outright).
         */
        int layer;
        int oldStructId;  ///< Structure id before Apply() (-1 = auto flood-fill).
        int newStructId;  ///< Structure id written by Apply() (-1 = auto flood-fill).
    };

    explicit SetTileStructureIdsCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 * Apply pushes the structure onto Tilemap's vector and captures the assigned
 * id. Revert removes the structure by that id, which is lossless only while
 * this structure is the last element of the vector - the LIFO undo order
 * guarantees it, because any command that appended a later structure is undone
 * first. RemoveNoProjectionStructure also clears the structureId of every tile
 * that referenced the structure, and this command keeps no tile snapshot
 * (unlike RemoveStructureCmd), so a Revert / Redo pair does not restore tile
 * membership.
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 * decrements ids > id. To keep undo lossless, the command snapshots which tiles
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 *
 * Apply appends the zone; Revert pops the last zone instead of searching for
 * it, and does not check that the popped zone is the one this command added.
 * It therefore relies on the LIFO undo order leaving this command's zone at
 * the end of Tilemap's vector. Zone indices are identity for
 * RemoveParticleZoneCmd, so any append that bypasses the undo stack while this
 * command sits on it makes Revert delete the wrong zone.
 */
class AddParticleZoneCmd : public EditorCommand
{
public:
    explicit AddParticleZoneCmd(ParticleZone zone)
        : m_Zone(std::move(zone))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 *
 * Mirrors the per-layer per-tile fields of TileLayer. Every default here matches the
 * corresponding TileLayer default, so a default-constructed instance pastes as "empty".
 */
struct ClipboardCellLayer
{
    int tileId = -1;                                      ///< Tileset id (-1 = empty cell).
    float rotation = 0.0f;                                ///< Rotation in degrees.
    TileStance stance = TileStance::Flat;                 ///< Ground vs upright role.
    ElevationRole elevationRole = ElevationRole::Ground;  ///< Rises to the cell's elevation.
    bool flipX = false;                                   ///< Mirror around the vertical axis.
    bool flipY = false;                                   ///< Mirror around the horizontal axis.
    /**
     * @brief Owning no-projection structure (-1 = auto flood-fill), captured one layer low.
     *
     * `PasteRegionCmd::ReadCellFrom` fills slot `i` with `GetTileStructureId(x, y, i)`, and
     * that accessor is 1-based: it reads internal layer `i - 1`. Slot 0 therefore always
     * holds -1, and the top layer's structure id is never captured. `WriteCellInto` applies
     * the identical shear, so a copy/paste round-trip is self-consistent, but a paste never
     * restores the top layer's structure membership.
     */
    int structureId = -1;
    bool ySortPlus = false;   ///< Tile Y-sorts with entities, player in front at equal Y.
    bool ySortMinus = false;  ///< Tile Y-sorts with entities, tile in front at equal Y.
    int animationMap = -1;    ///< Animated-tile id (-1 = not animated).
};

/**
 * @brief Snapshot of one tile across LAYER_COUNT layers plus the per-cell fields.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Captures everything `PasteRegionCmd` needs to clone a tile from one
 * map / location to another. NPCs, particle zones, and structures are not
 * captured (cross-map identity is non-trivial; documented in EDITOR.md).
 *
 * @warning LAYER_COUNT is a hard-coded 10, but Tilemap's layer stack is dynamic - a map
 * loaded with more than 10 `dynamicLayers` has layers this snapshot cannot represent.
 * `PasteRegionCmd::ReadCellFrom` and `WriteCellInto` both loop exactly LAYER_COUNT times,
 * so Ctrl+C / Ctrl+V on such a map silently ignores layers 10 and above: they are neither
 * copied from the source nor overwritten at the destination. Raising this constant is not
 * enough on its own - the copy would still be truncated for any map with more layers.
 * A second, independent off-by-one affects the structure ids; see
 * @ref ClipboardCellLayer::structureId.
 */
struct ClipboardCell
{
    static constexpr std::size_t LAYER_COUNT = 10;  ///< Layers a snapshot can hold; see warning.
    ClipboardCellLayer layers[LAYER_COUNT];         ///< Per-layer state, indexed by layer number.
    bool collision = false;                         ///< Per-cell blocking flag (not per-layer).
    bool navigation = false;  ///< Per-cell NPC walkability flag (not per-layer).
    int elevation = 0;        ///< Per-cell elevation in pixels (0 = ground level).
};

/**
 * @brief Rectangular region of tile snapshots used by Ctrl+C / Ctrl+V.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Cells that fell outside the map when the region was snapshotted stay
 * default-constructed; paste skips destinations outside the map with the same bounds
 * check, so a region straddling an edge round-trips without corrupting the map.
 */
struct ClipboardRegion
{
    int width = 0;                     ///< Region width in tiles.
    int height = 0;                    ///< Region height in tiles.
    std::vector<ClipboardCell> cells;  ///< size = width * height, row-major.

    /// @brief True when the region holds nothing pasteable.
    [[nodiscard]] bool Empty() const { return width <= 0 || height <= 0 || cells.empty(); }
};

/**
 * @brief Reflect a region in place around its geometric center.
 *
 * Cell positions swap (columns for X-axis, rows for Y-axis), and per-tile
 * each layer's flip flag on the chosen axis is toggled while rotation is
 * negated (rot -> fmod(360 - rot, 360)). The transform is an involution,
 * so applying it twice reproduces the original.
 *
 * @param region    Region to mutate in place.
 * @param flipXAxis True for X-reflection (mirror around vertical axis,
 *                  toggles flipX), false for Y-reflection (toggles flipY).
 */
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    /**
     * @brief Read a single tile (all 10 layers + per-tile fields) into a
     * ClipboardCell. Public so the editor's Ctrl+C path can snapshot a
     * region using the same logic this cmd uses for its own dest snapshot.
     */
    [[nodiscard]] static ClipboardCell ReadCellFrom(const Tilemap& tm, int x, int y);

    /**
     * @brief Snapshot a (width x height) region starting at (x, y) into a
     * ClipboardRegion. Used by Editor's Ctrl+C handler.
     */
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

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
 * reverse order. Used when one user action mutates multiple Tilemap surfaces -
 * G-mode structure assignment writes per-layer `stance` (TileStance::Structure)
 * and per-layer `structureId`, as a SetTileStancesCmd + SetTileStructureIdsCmd
 * pair. The editor commits these composites with UndoRedoStack::Push because
 * the tilemap is already mutated, so the children's Apply first runs on Redo.
 */
class CompositeCmd : public EditorCommand
{
public:
    CompositeCmd(std::string label, std::vector<std::unique_ptr<EditorCommand>> children)
        : m_Label(std::move(label)),
          m_Children(std::move(children))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
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
    /// One cell's before/after walkability. Navigation is a per-cell grid, so no layer.
    struct Entry
    {
        int tileX;         ///< Tile column.
        int tileY;         ///< Tile row.
        bool oldWalkable;  ///< Walkability before Apply(); restored by Revert().
        bool newWalkable;  ///< Walkability written by Apply().
    };

    explicit NavigationStrokeCmd(std::vector<Entry> entries)
        : m_Entries(std::move(entries))
    {
    }

    void Apply(Tilemap& tilemap, ecs::registry& npcs) override;
    void Revert(Tilemap& tilemap, ecs::registry& npcs) override;
    [[nodiscard]] std::string DebugLabel() const override;

    [[nodiscard]] const std::vector<Entry>& Entries() const { return m_Entries; }

private:
    std::vector<Entry> m_Entries;
    /// Detached NPC blueprints for the NPCs this stroke displaced: filled by Apply,
    /// re-spawned and cleared by Revert.
    std::vector<NpcRecord> m_ErasedNPCs;
};
