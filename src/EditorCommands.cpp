#include "EditorCommands.hpp"

#include "EntityStore.hpp"
#include "NavigationRecalc.hpp"
#include "NpcTag.hpp"
#include "Patrol.hpp"
#include "Tilemap.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

// Two passes: first move the cells to their mirrored positions, then fix up each cell's
// own orientation. Both are needed - swapping positions alone mirrors the layout but
// leaves every individual tile facing the wrong way.
void ReflectClipboardRegion(ClipboardRegion& region, bool flipXAxis)
{
    const int W = region.width;
    const int H = region.height;
    if (W <= 0 || H <= 0)
        return;

    if (flipXAxis)
    {
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W / 2; ++x)
            {
                std::swap(region.cells[static_cast<std::size_t>(y * W + x)],
                          region.cells[static_cast<std::size_t>(y * W + (W - 1 - x))]);
            }
        }
    }
    else
    {
        for (int y = 0; y < H / 2; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                std::swap(region.cells[static_cast<std::size_t>(y * W + x)],
                          region.cells[static_cast<std::size_t>((H - 1 - y) * W + x)]);
            }
        }
    }

    // Per-cell orientation fix-up. A mirror reverses the sense of a rotation - reflecting
    // a tile drawn at r degrees looks the same as drawing it at -r and then mirroring it
    // (M * R(r) == R(-r) * M). So each layer both toggles its flip flag on the reflected
    // axis and negates its rotation; doing only one of the two leaves rotated tiles
    // visibly wrong. Together they make the transform an involution.
    for (auto& cell : region.cells)
    {
        for (auto& layer : cell.layers)
        {
            if (flipXAxis)
                layer.flipX = !layer.flipX;
            else
                layer.flipY = !layer.flipY;
            // Normalize into [0, 360): fmod alone can return a negative for rotations
            // outside that range (e.g. 450 -> -90).
            float r = std::fmod(360.0f - layer.rotation, 360.0f);
            if (r < 0.0f)
                r += 360.0f;
            layer.rotation = r;
        }
    }
}

void PlaceTilesCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
    {
        tilemap.SetLayerTile(e.tileX, e.tileY, e.layer, e.newTileId);
        tilemap.SetLayerRotation(e.tileX, e.tileY, e.layer, e.newRotation);
        tilemap.SetLayerFlipX(e.tileX, e.tileY, e.layer, e.newFlipX);
        tilemap.SetLayerFlipY(e.tileX, e.tileY, e.layer, e.newFlipY);
    }
}

void PlaceTilesCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
    {
        tilemap.SetLayerTile(e.tileX, e.tileY, e.layer, e.oldTileId);
        tilemap.SetLayerRotation(e.tileX, e.tileY, e.layer, e.oldRotation);
        tilemap.SetLayerFlipX(e.tileX, e.tileY, e.layer, e.oldFlipX);
        tilemap.SetLayerFlipY(e.tileX, e.tileY, e.layer, e.oldFlipY);
    }
}

std::string PlaceTilesCmd::DebugLabel() const
{
    return "Place " + std::to_string(m_Entries.size()) + " tile(s)";
}

void CollisionToggleCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetTileCollision(e.tileX, e.tileY, e.newCollision);
}

void CollisionToggleCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetTileCollision(e.tileX, e.tileY, e.oldCollision);
}

std::string CollisionToggleCmd::DebugLabel() const
{
    return "Toggle collision (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void ElevationSetCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetElevation(e.tileX, e.tileY, e.newElevation);
}

void ElevationSetCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetElevation(e.tileX, e.tileY, e.oldElevation);
}

std::string ElevationSetCmd::DebugLabel() const
{
    return "Set elevation (" + std::to_string(m_Entries.size()) + " tile(s))";
}

namespace
{
// Entity of the NPC standing on (tileX, tileY), or nullopt. Tile-coord
// uniqueness is invariant in the editor's NPC click handler.
std::optional<ecs::entity> FindNPCAtTile(ecs::registry& npcs, int tileX, int tileY)
{
    std::optional<ecs::entity> found;
    npcs.each<const Patrol, const NpcTag>(
        [&](ecs::entity e, const Patrol& patrol)
        {
            if (patrol.tileX == tileX && patrol.tileY == tileY)
            {
                found = e;
            }
        });
    return found;
}
}  // namespace

PlaceNPCCmd::PlaceNPCCmd(NpcRecord npc)
    : m_TileX(npc.tileX),
      m_TileY(npc.tileY),
      m_Held(std::move(npc))
{
}

void PlaceNPCCmd::Apply(Tilemap& /*tilemap*/, ecs::registry& npcs)
{
    if (!m_Held.has_value())
        return;
    EntityStore::SpawnNpc(npcs, *m_Held);
    m_Held.reset();
}

void PlaceNPCCmd::Revert(Tilemap& /*tilemap*/, ecs::registry& npcs)
{
    std::optional<ecs::entity> e = FindNPCAtTile(npcs, m_TileX, m_TileY);
    if (!e.has_value())
        return;
    m_Held = EntityStore::SnapshotNpc(npcs, *e);
    EntityStore::Remove(npcs, *e);
}

std::string PlaceNPCCmd::DebugLabel() const
{
    return "Place NPC (" + std::to_string(m_TileX) + ", " + std::to_string(m_TileY) + ")";
}

void RemoveNPCCmd::Apply(Tilemap& /*tilemap*/, ecs::registry& npcs)
{
    std::optional<ecs::entity> e = FindNPCAtTile(npcs, m_TileX, m_TileY);
    if (!e.has_value())
        return;
    m_Held = EntityStore::SnapshotNpc(npcs, *e);
    EntityStore::Remove(npcs, *e);
}

void RemoveNPCCmd::Revert(Tilemap& /*tilemap*/, ecs::registry& npcs)
{
    if (!m_Held.has_value())
        return;
    EntityStore::SpawnNpc(npcs, *m_Held);
    m_Held.reset();
}

std::string RemoveNPCCmd::DebugLabel() const
{
    return "Remove NPC (" + std::to_string(m_TileX) + ", " + std::to_string(m_TileY) + ")";
}

void NavigationStrokeCmd::Apply(Tilemap& tilemap, ecs::registry& npcs)
{
    for (const Entry& e : m_Entries)
        tilemap.SetNavigation(e.tileX, e.tileY, e.newWalkable);

    // Snapshot any NPCs now displaced by tiles becoming non-walkable. The
    // snapshot is rebuilt fresh each Apply so Redo after intervening commands
    // still captures the right NPCs.
    m_ErasedNPCs = SnapshotAndEraseNPCsOnNonWalkable(tilemap, npcs);
    RebuildPatrolRoutes(tilemap, npcs);
}

void NavigationStrokeCmd::Revert(Tilemap& tilemap, ecs::registry& npcs)
{
    for (const Entry& e : m_Entries)
        tilemap.SetNavigation(e.tileX, e.tileY, e.oldWalkable);

    RestoreErasedNPCs(npcs, m_ErasedNPCs);
    RebuildPatrolRoutes(tilemap, npcs);
}

std::string NavigationStrokeCmd::DebugLabel() const
{
    return "Toggle navigation (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void SetTileStancesCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerStance(e.tileX, e.tileY, e.layer, e.newStance);
}

void SetTileStancesCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerStance(e.tileX, e.tileY, e.layer, e.oldStance);
}

std::string SetTileStancesCmd::DebugLabel() const
{
    return "Set tile stance (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void SetElevationRolesCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerElevationRole(e.tileX, e.tileY, e.layer, e.newRole);
}

void SetElevationRolesCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerElevationRole(e.tileX, e.tileY, e.layer, e.oldRole);
}

std::string SetElevationRolesCmd::DebugLabel() const
{
    return "Set elevation role (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void YSortPlusToggleCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerYSortPlus(e.tileX, e.tileY, e.layer, e.newFlag);
}

void YSortPlusToggleCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerYSortPlus(e.tileX, e.tileY, e.layer, e.oldFlag);
}

std::string YSortPlusToggleCmd::DebugLabel() const
{
    return "Toggle Y-sort-plus (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void YSortMinusToggleCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerYSortMinus(e.tileX, e.tileY, e.layer, e.newFlag);
}

void YSortMinusToggleCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetLayerYSortMinus(e.tileX, e.tileY, e.layer, e.oldFlag);
}

std::string YSortMinusToggleCmd::DebugLabel() const
{
    return "Toggle Y-sort-minus (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void SetTileAnimationCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetTileAnimation(e.tileX, e.tileY, e.layer, e.newAnimId);
}

void SetTileAnimationCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    // SetTileAnimation may stomp tiles[idx] when assigning a non-empty
    // animation - in particular, restoring an animId of -1 leaves tiles[idx]
    // at whatever value the prior animation set it to. So after restoring
    // animation, also explicitly restore the original tile id.
    for (const Entry& e : m_Entries)
    {
        tilemap.SetTileAnimation(e.tileX, e.tileY, e.layer, e.oldAnimId);
        tilemap.SetLayerTile(e.tileX, e.tileY, static_cast<std::size_t>(e.layer), e.oldTileId);
    }
}

std::string SetTileAnimationCmd::DebugLabel() const
{
    return "Set animation (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void SetTileStructureIdsCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetTileStructureId(e.tileX, e.tileY, e.layer, e.newStructId);
}

void SetTileStructureIdsCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    for (const Entry& e : m_Entries)
        tilemap.SetTileStructureId(e.tileX, e.tileY, e.layer, e.oldStructId);
}

std::string SetTileStructureIdsCmd::DebugLabel() const
{
    return "Set structure id (" + std::to_string(m_Entries.size()) + " tile(s))";
}

void CompositeCmd::Apply(Tilemap& tilemap, ecs::registry& npcs)
{
    for (auto& child : m_Children)
        child->Apply(tilemap, npcs);
}

void CompositeCmd::Revert(Tilemap& tilemap, ecs::registry& npcs)
{
    // Reverse order so child2.Revert undoes child2.Apply against the state
    // that existed after child1.Apply ran.
    for (auto it = m_Children.rbegin(); it != m_Children.rend(); ++it)
        (*it)->Revert(tilemap, npcs);
}

void AddStructureCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    m_StructureId = tilemap.AddNoProjectionStructure(m_LeftAnchor, m_RightAnchor, m_Name);
}

void AddStructureCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    if (m_StructureId < 0)
        return;
    tilemap.RemoveNoProjectionStructure(m_StructureId);
}

std::string AddStructureCmd::DebugLabel() const
{
    return "Add structure";
}

void RemoveStructureCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    const NoProjectionStructure* s = tilemap.GetNoProjectionStructure(m_Id);
    if (!s)
        return;

    m_Snapshot = *s;

    // Capture per-tile structureId references pointing at this id before
    // RemoveNoProjectionStructure clears them. Iterate all layers / tiles -
    // expensive but rare (right-click clear in G mode).
    // The loop counter is a 0-based layer index but GetTileStructureId/SetTileStructureId
    // take a 1-based one, so this scan is shifted by a layer relative to the editor call
    // sites (which pass m_CurrentLayer + 1).
    m_TileRefs.clear();
    int w = tilemap.GetMapWidth();
    int h = tilemap.GetMapHeight();
    for (std::size_t layer = 0; layer < tilemap.GetLayerCount(); ++layer)
    {
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                if (tilemap.GetTileStructureId(x, y, static_cast<int>(layer)) == m_Id)
                    m_TileRefs.push_back({x, y, static_cast<int>(layer)});
            }
        }
    }
    m_Captured = true;
    tilemap.RemoveNoProjectionStructure(m_Id);
}

void RemoveStructureCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    if (!m_Captured)
        return;
    tilemap.InsertNoProjectionStructureAt(static_cast<std::size_t>(m_Id), m_Snapshot);
    for (const TileRef& ref : m_TileRefs)
        tilemap.SetTileStructureId(ref.x, ref.y, ref.layer, m_Id);
}

std::string RemoveStructureCmd::DebugLabel() const
{
    return "Remove structure " + std::to_string(m_Id);
}

void AddParticleZoneCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    tilemap.AddParticleZone(m_Zone);
}

void AddParticleZoneCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    // LIFO invariant: this command's zone is the last one in the vector.
    auto* zones = tilemap.GetParticleZonesMutable();
    if (zones && !zones->empty())
        zones->pop_back();
}

std::string AddParticleZoneCmd::DebugLabel() const
{
    return "Add particle zone";
}

void RemoveParticleZoneCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    const auto* zones = tilemap.GetParticleZones();
    if (!zones || m_Index >= zones->size())
        return;
    m_Snapshot = (*zones)[m_Index];
    m_Captured = true;
    tilemap.RemoveParticleZone(m_Index);
}

void RemoveParticleZoneCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    if (!m_Captured)
        return;
    tilemap.InsertParticleZoneAt(m_Index, m_Snapshot);
}

std::string RemoveParticleZoneCmd::DebugLabel() const
{
    return "Remove particle zone " + std::to_string(m_Index);
}

void AddAnimatedTileCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    m_AnimId = tilemap.AddAnimatedTile(m_Anim);
}

void AddAnimatedTileCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    tilemap.PopLastAnimatedTile();
}

std::string AddAnimatedTileCmd::DebugLabel() const
{
    return "Add animation #" + std::to_string(m_AnimId);
}

ClipboardRegion PasteRegionCmd::SnapshotRegion(
    const Tilemap& tm, int x, int y, int width, int height)
{
    ClipboardRegion region;
    if (width <= 0 || height <= 0)
        return region;
    int mapW = tm.GetMapWidth();
    int mapH = tm.GetMapHeight();
    region.width = width;
    region.height = height;
    region.cells.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int dy = 0; dy < height; ++dy)
    {
        for (int dx = 0; dx < width; ++dx)
        {
            int tx = x + dx;
            int ty = y + dy;
            std::size_t idx = static_cast<std::size_t>(dy) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(dx);
            if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH)
                region.cells[idx] = ReadCellFrom(tm, tx, ty);
            // out-of-bounds source cells remain default (paste into clamped
            // dest will skip them too).
        }
    }
    return region;
}

// Snapshot one cell. The loop bound is ClipboardCell::LAYER_COUNT (a fixed 10), not
// tm.GetLayerCount(), because the destination struct is a fixed-size array. On a map with
// more than 10 dynamicLayers the extra layers are silently not copied - and WriteCellInto
// below is bounded the same way, so they are also not overwritten on paste. Reads past the
// tilemap's real layer count are safe: the Tilemap getters return field defaults for an
// out-of-range layer.
//
// Note the odd one out: `layer` is 0-based for every accessor here EXCEPT
// GetTileStructureId, which takes a 1-based index (it reads internal layer `layer - 1`).
// WriteCellInto has the same asymmetry, so a copy/paste round-trip is self-consistent, but
// the structureId a cell carries is the one from the layer below it: slot 0 always holds
// -1, and layer 9's structureId is never copied at all.
ClipboardCell PasteRegionCmd::ReadCellFrom(const Tilemap& tm, int x, int y)
{
    ClipboardCell cell;
    for (std::size_t layer = 0; layer < ClipboardCell::LAYER_COUNT; ++layer)
    {
        cell.layers[layer].tileId = tm.GetLayerTile(x, y, layer);
        cell.layers[layer].rotation = tm.GetLayerRotation(x, y, layer);
        cell.layers[layer].stance = tm.GetLayerStance(x, y, layer);
        cell.layers[layer].elevationRole = tm.GetLayerElevationRole(x, y, layer);
        cell.layers[layer].flipX = tm.GetLayerFlipX(x, y, layer);
        cell.layers[layer].flipY = tm.GetLayerFlipY(x, y, layer);
        cell.layers[layer].structureId = tm.GetTileStructureId(x, y, static_cast<int>(layer));
        cell.layers[layer].ySortPlus = tm.GetLayerYSortPlus(x, y, layer);
        cell.layers[layer].ySortMinus = tm.GetLayerYSortMinus(x, y, layer);
        cell.layers[layer].animationMap = tm.GetTileAnimation(x, y, static_cast<int>(layer));
    }
    cell.collision = tm.GetTileCollision(x, y);
    cell.navigation = tm.GetNavigation(x, y);
    cell.elevation = tm.GetElevation(x, y);
    return cell;
}

void PasteRegionCmd::WriteCellInto(Tilemap& tm, int destX, int destY, const ClipboardCell& cell)
{
    for (std::size_t layer = 0; layer < ClipboardCell::LAYER_COUNT; ++layer)
    {
        tm.SetLayerTile(destX, destY, layer, cell.layers[layer].tileId);
        tm.SetLayerRotation(destX, destY, layer, cell.layers[layer].rotation);
        tm.SetLayerStance(destX, destY, layer, cell.layers[layer].stance);
        tm.SetLayerElevationRole(destX, destY, layer, cell.layers[layer].elevationRole);
        tm.SetLayerFlipX(destX, destY, layer, cell.layers[layer].flipX);
        tm.SetLayerFlipY(destX, destY, layer, cell.layers[layer].flipY);
        tm.SetTileStructureId(
            destX, destY, static_cast<int>(layer), cell.layers[layer].structureId);
        tm.SetLayerYSortPlus(destX, destY, layer, cell.layers[layer].ySortPlus);
        tm.SetLayerYSortMinus(destX, destY, layer, cell.layers[layer].ySortMinus);
        tm.SetTileAnimation(destX, destY, static_cast<int>(layer), cell.layers[layer].animationMap);
    }
    tm.SetTileCollision(destX, destY, cell.collision);
    tm.SetNavigation(destX, destY, cell.navigation);
    tm.SetElevation(destX, destY, cell.elevation);
}

void PasteRegionCmd::Apply(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    if (m_Source.Empty())
        return;

    int mapW = tilemap.GetMapWidth();
    int mapH = tilemap.GetMapHeight();

    // Capture destination state on first Apply only (Redo reuses the same
    // snapshot so repeated round-trips don't drift).
    if (!m_Captured)
    {
        m_DestSnapshot.width = m_Source.width;
        m_DestSnapshot.height = m_Source.height;
        m_DestSnapshot.cells.resize(static_cast<std::size_t>(m_Source.width) *
                                    static_cast<std::size_t>(m_Source.height));
        for (int dy = 0; dy < m_Source.height; ++dy)
        {
            for (int dx = 0; dx < m_Source.width; ++dx)
            {
                int x = m_DestX + dx;
                int y = m_DestY + dy;
                std::size_t idx =
                    static_cast<std::size_t>(dy) * static_cast<std::size_t>(m_Source.width) +
                    static_cast<std::size_t>(dx);
                if (x >= 0 && x < mapW && y >= 0 && y < mapH)
                    m_DestSnapshot.cells[idx] = ReadCellFrom(tilemap, x, y);
                // out-of-bounds dest cells stay default-constructed; Revert
                // skips them via the same bounds check below.
            }
        }
        m_Captured = true;
    }

    // Write source cells into destination (clamped to map bounds).
    for (int dy = 0; dy < m_Source.height; ++dy)
    {
        for (int dx = 0; dx < m_Source.width; ++dx)
        {
            int x = m_DestX + dx;
            int y = m_DestY + dy;
            std::size_t idx =
                static_cast<std::size_t>(dy) * static_cast<std::size_t>(m_Source.width) +
                static_cast<std::size_t>(dx);
            if (x >= 0 && x < mapW && y >= 0 && y < mapH)
                WriteCellInto(tilemap, x, y, m_Source.cells[idx]);
        }
    }
}

void PasteRegionCmd::Revert(Tilemap& tilemap, ecs::registry& /*npcs*/)
{
    if (!m_Captured)
        return;
    int mapW = tilemap.GetMapWidth();
    int mapH = tilemap.GetMapHeight();
    for (int dy = 0; dy < m_DestSnapshot.height; ++dy)
    {
        for (int dx = 0; dx < m_DestSnapshot.width; ++dx)
        {
            int x = m_DestX + dx;
            int y = m_DestY + dy;
            std::size_t idx =
                static_cast<std::size_t>(dy) * static_cast<std::size_t>(m_DestSnapshot.width) +
                static_cast<std::size_t>(dx);
            if (x >= 0 && x < mapW && y >= 0 && y < mapH)
                WriteCellInto(tilemap, x, y, m_DestSnapshot.cells[idx]);
        }
    }
}

std::string PasteRegionCmd::DebugLabel() const
{
    return "Paste " + std::to_string(m_Source.width) + "x" + std::to_string(m_Source.height) +
           " region";
}
