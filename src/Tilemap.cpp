#include "Tilemap.hpp"

#include "AssetRegistry.hpp"
#include "Dialogue.hpp"
#include "DialogueStore.hpp"
#include "EntityStore.hpp"
#include "Frustum.hpp"
#include "Logger.hpp"
#include "NpcRecord.hpp"
#include "NpcTag.hpp"
#include "Patrol.hpp"
#include "SceneMath.hpp"
#include "TileRole.hpp"
#include "WorldServices.hpp"

#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <json.hpp>
#include <random>
#include <sstream>
#include <vector>

// Note: STB_IMAGE_IMPLEMENTATION is already defined in Texture.cpp
// Only the header is needed here, for the function declarations.
#include <stb_image.h>

#include "MathConstants.hpp"

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Tilemap";

// Extra scan padding so upright structures anchored just outside the cull rect
// still contribute their tiles: their artwork is drawn above the base row, so a
// structure whose base is off-screen can still have visible upper tiles.
constexpr float STRUCTURE_SCAN_PADDING_TILES = 8.0f;

// How many rows north of the visible range RenderWorld3D still scans for upright
// tiles. Upright artwork is drawn above its base row, so a structure standing
// just inside the range has its upper tiles on rows outside it; without the
// margin, tall buildings lose their tops as the camera approaches from the south.
// Sized for the tallest plausible structure rather than tuned by eye.
constexpr int UPRIGHT_SCAN_MARGIN_TILES = 16;

// First layer whose artwork is object/foreground rather than terrain. Layers 0-1
// are Ground and Ground Detail; 2+ are Objects, Objects2, Objects3, Foreground
// and the overlays. Read by the depth-queue promotion gate and, once per legacy
// map, by the stance migration.
constexpr size_t FIRST_OBJECT_LAYER = 2;

// Translate a layer authored before TileStance existed.
//
//   noProjection                          -> Structure
//   y-sort on an object/foreground layer  -> Wall when a 4-connected neighbour
//                                            also stands up, else Prop
//   anything else                         -> Flat
//
// The y-sort flags never had a geometric meaning in the flat pipeline - only
// noProjection did - but the 3D path briefly stood them up, so maps authored in
// that window rely on it for props. Restricting the promotion to object and
// foreground layers is what keeps the ~170 Ground Detail decals on the floor
// where they belong.
//
// Wall-vs-Prop reproduces the adjacency rule the alternative design would have
// applied at render time forever, so as a one-OFF seed it cannot be worse, and
// every cell it guesses wrong is one click to change.
//
// This is the only place the layer index or a cell's neighbours may influence a
// stance - see the note in TileRole.hpp. Nothing at render time may consult
// either.
void MigrateLayerStance(TileLayer& layer,
                        const std::vector<uint8_t>& legacyNoProjection,
                        size_t layerIndex,
                        int width,
                        int height)
{
    const size_t mapSize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const bool objectLayer = layerIndex >= FIRST_OBJECT_LAYER;

    const auto wasNoProjection = [&legacyNoProjection](size_t i)
    { return i < legacyNoProjection.size() && legacyNoProjection[i] != 0; };

    // Which cells stand up at all, resolved before any of them is classified so
    // the neighbour test below sees the whole picture.
    std::vector<uint8_t> upright(mapSize, 0);
    for (size_t i = 0; i < mapSize; ++i)
    {
        const bool ySorted = (i < layer.ySortPlus.size() && layer.ySortPlus[i]) ||
                             (i < layer.ySortMinus.size() && layer.ySortMinus[i]);
        upright[i] = (wasNoProjection(i) || (objectLayer && ySorted)) ? 1 : 0;
    }

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t i =
                static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (upright[i] == 0)
            {
                continue;
            }
            if (wasNoProjection(i))
            {
                layer.stance[i] = TileStance::Structure;
                continue;
            }

            const bool hasUprightNeighbour =
                (x > 0 && upright[i - 1] != 0) || (x + 1 < width && upright[i + 1] != 0) ||
                (y > 0 && upright[i - static_cast<size_t>(width)] != 0) ||
                (y + 1 < height && upright[i + static_cast<size_t>(width)] != 0);

            layer.stance[i] = hasUprightNeighbour ? TileStance::Wall : TileStance::Prop;
        }
    }
}

// Compute one edge point of an upright structure at an arbitrary world Y.
// Accepts continuous Y so particles can stay locked to the same mesh the tiles
// are drawn on. The structure stands on its anchor base and extrudes straight
// up: u picks the column across the base, v the height above it.
glm::vec2 ComputeEdgePoint(float anchorMinScreenX,
                           float anchorMaxScreenX,
                           float bottomScreenY,
                           int layerMinY,
                           int layerMaxY,
                           int tileHeight,
                           int structureWidthTiles,
                           int edgeIndex,
                           float worldTileY)
{
    float heightTiles = std::max(1.0f, static_cast<float>(layerMaxY - layerMinY) + 1.0f);

    glm::vec2 baseLeft(anchorMinScreenX, bottomScreenY);
    glm::vec2 baseRight(anchorMaxScreenX, bottomScreenY);

    float safeWidth = static_cast<float>(std::max(1, structureWidthTiles));
    float u = static_cast<float>(edgeIndex) / safeWidth;

    float tileRow = static_cast<float>(layerMaxY) - worldTileY;
    float v = (tileRow + 1.0f) / heightTiles;
    v = std::max(-2.0f, std::min(2.0f, v));

    float heightWorld = heightTiles * static_cast<float>(tileHeight);

    glm::vec2 basePoint = baseLeft + u * (baseRight - baseLeft);
    return glm::vec2(basePoint.x, basePoint.y - v * heightWorld);
}

}  // namespace

// Construct an empty tilemap at the default size with the standard 10-layer stack
// (5 background + 5 foreground). Tiles are left blank; the terrain is filled in
// later by GenerateDefaultMap once a tileset is loaded (via SetTilemapSize).
Tilemap::Tilemap()
{
    // Allocate storage for all layers using row-major layout: size = width * height
    const size_t mapSize = MapCellCount();

    // Collision and navigation maps
    m_CollisionMap.Resize(m_MapWidth, m_MapHeight);
    m_NavigationMap.Resize(m_MapWidth, m_MapHeight);

    m_Layers.clear();
    m_Layers.emplace_back("Ground", 0, true);
    m_Layers.emplace_back("Ground Detail", 10, true);
    m_Layers.emplace_back("Objects", 20, true);
    m_Layers.emplace_back("Objects2", 30, true);
    m_Layers.emplace_back("Objects3", 40, true);
    m_Layers.emplace_back("Foreground", 100, false);
    m_Layers.emplace_back("Foreground2", 110, false);
    m_Layers.emplace_back("Overlay", 120, false);
    m_Layers.emplace_back("Overlay2", 130, false);
    m_Layers.emplace_back("Overlay3", 140, false);

    // Resize all layers to map size
    for (auto& layer : m_Layers)
    {
        layer.Resize(mapSize);
    }

    // Initialize animation map (all tiles start with no animation)
    m_TileAnimationMap.assign(mapSize, -1);

    // Defer map generation until tileset is loaded
    // GenerateDefaultMap() will be called from SetTilemapSize() after LoadCombinedTilesets()
}

Tilemap::~Tilemap() = default;

// Full O(width*height*layers) rebuild of the per-(layer, structure) bounding-box
// cache used to place warped no-projection structures without a full-map scan on
// every query. Clears the dirty flag and any pending single-structure dirty keys.
void Tilemap::RebuildStructureBoundsCache() const
{
    m_StructureBoundsCache.clear();
    for (size_t li = 0; li < m_Layers.size(); ++li)
    {
        const TileLayer& layer = m_Layers[li];
        for (int y = 0; y < m_MapHeight; ++y)
        {
            for (int x = 0; x < m_MapWidth; ++x)
            {
                size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_MapWidth) +
                             static_cast<size_t>(x);
                if (idx >= layer.structureId.size())
                    continue;
                int sid = layer.structureId[idx];
                if (sid < 0)
                    continue;

                int64_t key = (static_cast<int64_t>(li) << 32) | static_cast<int64_t>(sid);
                auto it = m_StructureBoundsCache.find(key);
                if (it == m_StructureBoundsCache.end())
                {
                    m_StructureBoundsCache[key] = {x, x, y, y};
                }
                else
                {
                    it->second.minX = std::min(it->second.minX, x);
                    it->second.maxX = std::max(it->second.maxX, x);
                    it->second.minY = std::min(it->second.minY, y);
                    it->second.maxY = std::max(it->second.maxY, y);
                }
            }
        }
    }
    m_StructureBoundsCacheDirty = false;
    m_DirtyStructureKeys.clear();
}

// Mark the whole structure-bounds cache stale; it is lazily rebuilt on the next
// GetCachedStructureBounds call.
void Tilemap::InvalidateStructureBoundsCache()
{
    m_StructureBoundsCacheDirty = true;
    m_DirtyStructureKeys.clear();
}

// Incrementally patch the structure-bounds cache after one tile's structure id
// changes: grow the new structure's box in O(1), and mark the old structure dirty
// for a lazy single-structure re-scan (its box may need to shrink). No-op when a
// full rebuild is already pending.
void Tilemap::InvalidateStructureBoundsForTile(
    size_t layerIdx, int x, int y, int oldStructId, int newStructId)
{
    if (m_StructureBoundsCacheDirty)
    {
        return;  // Full rebuild already pending.
    }

    // Expand bounds for the new structure - O(1).
    if (newStructId >= 0)
    {
        int64_t key = (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(newStructId);
        auto it = m_StructureBoundsCache.find(key);
        if (it != m_StructureBoundsCache.end())
        {
            it->second.minX = std::min(it->second.minX, x);
            it->second.maxX = std::max(it->second.maxX, x);
            it->second.minY = std::min(it->second.minY, y);
            it->second.maxY = std::max(it->second.maxY, y);
        }
        else
        {
            m_StructureBoundsCache[key] = {x, x, y, y};
        }
        // The bounds set above already include this tile, so any prior dirty mark on the
        // new structure is satisfied.
        m_DirtyStructureKeys.erase(key);
    }

    // Old-structure bounds may need to shrink - mark dirty for lazy re-scan.
    if (oldStructId >= 0 && oldStructId != newStructId)
    {
        int64_t key = (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(oldStructId);
        m_DirtyStructureKeys.insert(key);
    }
}

// Recompute the bounding box of a single structure by scanning only that
// structure's layer, used to satisfy a lazy dirty mark without a full rebuild.
void Tilemap::RebuildSingleStructureBounds(size_t layerIdx, int structId, int64_t key) const
{
    m_StructureBoundsCache.erase(key);

    if (layerIdx >= m_Layers.size())
    {
        return;
    }

    const TileLayer& layer = m_Layers[layerIdx];
    for (int y = 0; y < m_MapHeight; ++y)
    {
        for (int x = 0; x < m_MapWidth; ++x)
        {
            size_t idx =
                static_cast<size_t>(y) * static_cast<size_t>(m_MapWidth) + static_cast<size_t>(x);
            if (idx >= layer.structureId.size())
            {
                continue;
            }
            if (layer.structureId[idx] != structId)
            {
                continue;
            }

            auto it = m_StructureBoundsCache.find(key);
            if (it == m_StructureBoundsCache.end())
            {
                m_StructureBoundsCache[key] = {x, x, y, y};
            }
            else
            {
                it->second.minX = std::min(it->second.minX, x);
                it->second.maxX = std::max(it->second.maxX, x);
                it->second.minY = std::min(it->second.minY, y);
                it->second.maxY = std::max(it->second.maxY, y);
            }
        }
    }
}

// Return the cached tile-space bounding box of (layerIdx, structId), rebuilding
// the whole cache or just this structure first if either is marked dirty. Returns
// nullptr when the structure has no tiles in that layer.
const Tilemap::StructureBounds* Tilemap::GetCachedStructureBounds(size_t layerIdx,
                                                                  int structId) const
{
    if (m_StructureBoundsCacheDirty)
    {
        RebuildStructureBoundsCache();
    }

    int64_t key = (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(structId);

    // Lazy re-scan if this structure was marked dirty.
    if (m_DirtyStructureKeys.contains(key))
    {
        RebuildSingleStructureBounds(layerIdx, structId, key);
        m_DirtyStructureKeys.erase(key);
    }

    auto it = m_StructureBoundsCache.find(key);
    if (it != m_StructureBoundsCache.end())
    {
        return &it->second;
    }
    return nullptr;
}

// Precompute, for every tile id in the atlas, whether it is fully transparent
// (RGBA alpha 0, or the pure-black / pure-white key colors for RGB tilesets) so
// per-tile render culling can skip the live pixel scan. Indexed by tile id.
void Tilemap::BuildTransparencyCache()
{
    if (!m_TilesetData || m_TilesetChannels == 0)
    {
        m_TransparencyCacheBuilt = false;
        return;
    }

    int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    int dataTilesPerCol = m_TilesetDataHeight / m_TileHeight;
    int totalTiles = dataTilesPerRow * dataTilesPerCol;

    m_TileTransparencyCache.resize(totalTiles, 1);

    for (int tileID = 0; tileID < totalTiles; ++tileID)
    {
        int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
        int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

        bool isTransparent = true;

        // Scan pixels in this tile
        for (int y = 0; y < m_TileHeight && isTransparent; ++y)
        {
            for (int x = 0; x < m_TileWidth && isTransparent; ++x)
            {
                int px = tilesetX + x;
                int py = tilesetY + y;

                if (px >= m_TilesetDataWidth || py >= m_TilesetDataHeight)
                    continue;

                int index = (py * m_TilesetDataWidth + px) * m_TilesetChannels;

                if (m_TilesetChannels == 4)
                {
                    unsigned char alpha = m_TilesetData[index + 3];
                    if (alpha > 0)
                        isTransparent = false;
                }
                else if (m_TilesetChannels == 3)
                {
                    unsigned char r = m_TilesetData[index];
                    unsigned char g = m_TilesetData[index + 1];
                    unsigned char b = m_TilesetData[index + 2];
                    bool isPureBlack = (r == 0 && g == 0 && b == 0);
                    bool isPureWhite = (r == 255 && g == 255 && b == 255);
                    if (!isPureBlack && !isPureWhite)
                        isTransparent = false;
                }
            }
        }

        m_TileTransparencyCache[tileID] = static_cast<uint8_t>(isTransparent);
    }

    m_TransparencyCacheBuilt = true;
    Logger::DebugF(LOG_SUBSYSTEM, "Built transparency cache for {} tiles", totalTiles);
}

// Load one or more tileset images and stack them vertically into a single atlas
// texture (narrower sheets padded with transparency on the right). All sheets must
// share a channel count. Uploads the GL-flipped atlas to the GPU, records the
// tileset-only baseline height for PackAdditionalSheets, and builds the
// transparency cache. Returns false on any load or channel-mismatch error.
bool Tilemap::LoadCombinedTilesets(const std::vector<std::string>& paths,
                                   int tileWidth,
                                   int tileHeight)
{
    if (paths.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "No tileset paths provided!");
        return false;
    }

    m_TileWidth = tileWidth;
    m_TileHeight = tileHeight;

    if (m_TileWidth <= 0 || m_TileHeight <= 0)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Invalid tile dimensions: {}x{}", m_TileWidth, m_TileHeight);
        return false;
    }

    // Load all tilesets as raw data
    stbi_set_flip_vertically_on_load(false);

    struct TilesetData
    {
        unsigned char* data;
        int width;
        int height;
        int channels;
    };

    std::vector<TilesetData> tilesets;
    tilesets.reserve(paths.size());

    // Load all tilesets
    for (size_t i = 0; i < paths.size(); ++i)
    {
        int width, height, channels;
        unsigned char* data = stbi_load(paths[i].c_str(), &width, &height, &channels, 0);
        if (!data)
        {
            Logger::ErrorF(LOG_SUBSYSTEM, "Could not load tileset {}: {}", i + 1, paths[i]);
            // Clean up already loaded tilesets
            for (auto& ts : tilesets)
            {
                stbi_image_free(ts.data);
            }
            return false;
        }
        tilesets.push_back({data, width, height, channels});
    }

    // Verify at least one tileset was loaded
    if (tilesets.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "No tilesets were loaded!");
        return false;
    }

    // Verify all tilesets have same channels
    int channels = tilesets[0].channels;
    for (size_t i = 1; i < tilesets.size(); ++i)
    {
        if (tilesets[i].channels != channels)
        {
            Logger::ErrorF(
                LOG_SUBSYSTEM,
                "Tilesets must have the same number of channels! Tileset 1: {}, Tileset {}: {}",
                channels,
                i + 1,
                tilesets[i].channels);
            // Clean up
            for (auto& ts : tilesets)
            {
                stbi_image_free(ts.data);
            }
            return false;
        }
    }

    // Find maximum width for the combined tileset
    int combinedWidth = tilesets[0].width;
    int combinedHeight = 0;
    for (const auto& ts : tilesets)
    {
        combinedWidth = std::max(combinedWidth, ts.width);
        combinedHeight += ts.height;
    }

    // Allocate combined data with RAII to prevent leaks on exceptions
    size_t combinedSize = static_cast<size_t>(combinedWidth) * static_cast<size_t>(combinedHeight) *
                          static_cast<size_t>(channels);
    auto combinedData = std::make_unique<unsigned char[]>(combinedSize);

    // Initialize combined data to transparent (0)
    memset(combinedData.get(), 0, combinedSize);

    // Copy each tileset vertically, stacking them
    int currentY = 0;
    for (size_t i = 0; i < tilesets.size(); ++i)
    {
        const auto& ts = tilesets[i];
        for (int y = 0; y < ts.height; ++y)
        {
            // Calculate offsets with explicit bounds check
            size_t destOffset = static_cast<size_t>(currentY + y) *
                                static_cast<size_t>(combinedWidth) * static_cast<size_t>(channels);
            size_t srcOffset = static_cast<size_t>(y) * static_cast<size_t>(ts.width) *
                               static_cast<size_t>(channels);
            size_t copySize = static_cast<size_t>(ts.width) * static_cast<size_t>(channels);

            // Verify bounds before copy
            if (destOffset + copySize <= combinedSize)
            {
                memcpy(combinedData.get() + destOffset, ts.data + srcOffset, copySize);
            }
            // Rest of the row is already transparent from memset
        }
        currentY += ts.height;
    }

    // Create OpenGL texture from combined data
    // Flip vertically for OpenGL (origin at bottom-left)
    auto flippedData = std::make_unique<unsigned char[]>(combinedSize);
    for (int y = 0; y < combinedHeight; ++y)
    {
        int srcY = combinedHeight - 1 - y;
        memcpy(flippedData.get() + static_cast<size_t>(y) * static_cast<size_t>(combinedWidth) *
                                       static_cast<size_t>(channels),
               combinedData.get() + static_cast<size_t>(srcY) * static_cast<size_t>(combinedWidth) *
                                        static_cast<size_t>(channels),
               static_cast<size_t>(combinedWidth) * static_cast<size_t>(channels));
    }

    // Load combined texture
    if (!m_TilesetTexture.LoadFromData(
            flippedData.get(), combinedWidth, combinedHeight, channels, false))
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to create combined texture!");
        for (auto& ts : tilesets)
        {
            stbi_image_free(ts.data);
        }
        return false;
    }

    // Store combined data for transparency checking (don't flip for data checking).
    // Transfer ownership from the local unique_ptr<unsigned char[]> to TilesetDataPtr.
    m_TilesetData = TilesetDataPtr(combinedData.release(), +[](unsigned char* p) { delete[] p; });
    m_TilesetDataWidth = combinedWidth;
    m_TilesetDataHeight = combinedHeight;
    m_TilesetChannels = channels;

    m_TilesetWidth = combinedWidth;
    m_TilesetHeight = combinedHeight;
    m_TilesPerRow = m_TilesetWidth / m_TileWidth;
    m_TilesetOnlyHeight = combinedHeight;  // baseline for PackAdditionalSheets

    Logger::InfoF(
        LOG_SUBSYSTEM, "Combined tileset dimensions: {}x{}", m_TilesetWidth, m_TilesetHeight);
    for (size_t i = 0; i < tilesets.size(); ++i)
    {
        Logger::InfoF(LOG_SUBSYSTEM,
                      "  Tileset {}: {}x{} ({} tiles wide) - {}",
                      i + 1,
                      tilesets[i].width,
                      tilesets[i].height,
                      tilesets[i].width / m_TileWidth,
                      paths[i]);
    }
    if (tilesets.size() > 1)
    {
        bool differentWidths = false;
        for (size_t i = 1; i < tilesets.size(); ++i)
        {
            if (tilesets[i].width != tilesets[0].width)
            {
                differentWidths = true;
                break;
            }
        }
        if (differentWidths)
        {
            Logger::Info(LOG_SUBSYSTEM,
                         "  Note: Tilesets have different widths. Narrower tilesets padded with "
                         "transparency.");
        }
    }
    Logger::InfoF(LOG_SUBSYSTEM, "Tile size: {}x{}", m_TileWidth, m_TileHeight);
    Logger::InfoF(LOG_SUBSYSTEM, "Tiles per row: {}", m_TilesPerRow);
    Logger::InfoF(LOG_SUBSYSTEM,
                  "Total tiles: {}",
                  (m_TilesetDataWidth / m_TileWidth) * (m_TilesetDataHeight / m_TileHeight));

    // Clean up temporary data (flippedData is auto-freed by unique_ptr)
    for (auto& ts : tilesets)
    {
        stbi_image_free(ts.data);
    }

    // Build transparency cache for all tiles
    BuildTransparencyCache();

    return true;
}

// Re-pack the atlas: truncate back to the tileset-only baseline, then append the
// given character/sprite sheets below it, growing the atlas height and re-uploading
// it. Records each sheet's GL-row offset in m_CharacterAtlasOffsets. Passing an
// empty list just shrinks the atlas back to the tileset baseline. Sheets must match
// the atlas channel count and fit within its width. Returns false on mismatch or
// re-upload failure.
bool Tilemap::PackAdditionalSheets(const std::vector<AtlasPackEntry>& sheets)
{
    if (!m_TilesetData || m_TilesetDataWidth <= 0 || m_TilesetOnlyHeight <= 0)
    {
        Logger::Error(LOG_SUBSYSTEM, "PackAdditionalSheets: no tileset atlas loaded");
        return false;
    }

    // Clear any previously-packed offsets; the atlas truncates back to the
    // tileset-only baseline before re-appending the new set, so old keys are
    // no longer valid.
    m_CharacterAtlasOffsets.clear();

    if (sheets.empty())
    {
        // Truncating to baseline with no characters is a valid request. Carry
        // on so the atlas shrinks back to tileset-only and re-uploads.
    }

    // All sheets must share channels with the atlas, and fit horizontally.
    int extraHeight = 0;
    for (const auto& entry : sheets)
    {
        const Texture* tex = entry.texture;
        if (tex == nullptr)
        {
            continue;
        }
        if (tex->GetChannels() != m_TilesetChannels)
        {
            Logger::ErrorF(LOG_SUBSYSTEM,
                           "PackAdditionalSheets: sheet '{}' has {} channels, atlas has {}",
                           entry.key,
                           tex->GetChannels(),
                           m_TilesetChannels);
            return false;
        }
        if (tex->GetWidth() > m_TilesetDataWidth)
        {
            Logger::ErrorF(LOG_SUBSYSTEM,
                           "PackAdditionalSheets: sheet '{}' is {}px wide, atlas is {}px",
                           entry.key,
                           tex->GetWidth(),
                           m_TilesetDataWidth);
            return false;
        }
        extraHeight += tex->GetHeight();
    }

    const int oldHeight = m_TilesetOnlyHeight;  // baseline, ignoring any prior characters
    const int newHeight = oldHeight + extraHeight;
    const int width = m_TilesetDataWidth;
    const int channels = m_TilesetChannels;
    const size_t rowStride = static_cast<size_t>(width) * static_cast<size_t>(channels);
    const size_t newSize = static_cast<size_t>(newHeight) * rowStride;

    auto newData = std::make_unique<unsigned char[]>(newSize);
    std::memset(newData.get(), 0, newSize);

    // Carry over the existing tileset data unchanged.
    std::memcpy(newData.get(), m_TilesetData.get(), static_cast<size_t>(oldHeight) * rowStride);

    // Append each sheet flipped row-for-row so that, after the uniform CPU pre-flip
    // below, the sheet's atlas sub-region preserves the source's m_ImageData layout
    // exactly (just relocated):
    //     atlas_m_ImageData[atlasOffset.y + k] == source_m_ImageData[k]
    // This holds regardless of how the source was uploaded (LoadFromFile's stbi
    // bottom-up rows or LoadFromData(false)'s top-down rows) - only the relative row
    // ordering within each region matters. So both characters (DrawSpriteRegion) and
    // sky elements (DrawSpriteAtlas) sample the same pixels from the atlas as they
    // did from their own textures.
    //
    // atlasOffset.y is the GL-row of this sheet's first GL row
    // (newHeight - currentY - sheetH): characters add their spriteCoords.y to it;
    // sky elements use it as the origin of a uv-min/uv-max rect.
    int currentY = oldHeight;
    for (const auto& entry : sheets)
    {
        const Texture* tex = entry.texture;
        if (tex == nullptr)
        {
            continue;
        }
        const auto& srcPixels = tex->GetImageData();
        const int sheetW = tex->GetWidth();
        const int sheetH = tex->GetHeight();
        const size_t srcRowStride = static_cast<size_t>(sheetW) * static_cast<size_t>(channels);

        for (int y = 0; y < sheetH; ++y)
        {
            const size_t destOffset = static_cast<size_t>(currentY + y) * rowStride;
            const size_t srcOffset = static_cast<size_t>(sheetH - 1 - y) * srcRowStride;
            std::memcpy(newData.get() + destOffset, srcPixels.data() + srcOffset, srcRowStride);
        }

        const float glOffsetY = static_cast<float>(newHeight - currentY - sheetH);
        m_CharacterAtlasOffsets[entry.key] = glm::vec2(0.0f, glOffsetY);
        currentY += sheetH;
    }

    // Flip vertically for OpenGL upload (Vulkan compensates via UV).
    auto flippedData = std::make_unique<unsigned char[]>(newSize);
    for (int y = 0; y < newHeight; ++y)
    {
        const int srcY = newHeight - 1 - y;
        std::memcpy(flippedData.get() + static_cast<size_t>(y) * rowStride,
                    newData.get() + static_cast<size_t>(srcY) * rowStride,
                    rowStride);
    }

    if (!m_TilesetTexture.LoadFromData(flippedData.get(), width, newHeight, channels, false))
    {
        Logger::Error(LOG_SUBSYSTEM, "PackAdditionalSheets: failed to re-upload atlas");
        return false;
    }

    m_TilesetData = TilesetDataPtr(newData.release(), +[](unsigned char* p) { delete[] p; });
    m_TilesetDataHeight = newHeight;
    m_TilesetHeight = newHeight;

    Logger::InfoF(LOG_SUBSYSTEM,
                  "Atlas grown to {}x{} with {} character sheet(s)",
                  width,
                  newHeight,
                  sheets.size());

    // Tile transparency cache is keyed by tile ID; existing IDs still map to
    // their original positions, so no rebuild is needed.

    return true;
}

std::optional<glm::vec2> Tilemap::GetCharacterAtlasOffset(const std::string& key) const
{
    auto it = m_CharacterAtlasOffsets.find(key);
    if (it == m_CharacterAtlasOffsets.end())
    {
        return std::nullopt;
    }
    return it->second;
}

// Resize the map to width x height, rebuilding the 10-layer stack plus the
// collision, navigation, elevation, corner-cut, and animation buffers from scratch
// (all cleared). Regenerates a random default map when generateMap is set and a
// tileset is loaded. Invalidates the structure-bounds cache.
void Tilemap::SetTilemapSize(int width, int height, bool generateMap)
{
    m_MapWidth = width;
    m_MapHeight = height;

    const size_t mapSize = static_cast<size_t>(m_MapWidth) * static_cast<size_t>(m_MapHeight);

    m_Elevation.assign(mapSize, 0);
    m_ElevationRegionIds.clear();
    m_ElevationRegionIdsDirty = true;

    // Initialize dynamic layers (10 total: 5 background, 5 foreground)
    m_Layers.clear();
    m_Layers.reserve(10);

    // Background layers (rendered before player)
    m_Layers.push_back(TileLayer("Ground", 0, true));          // Layer 0: Base terrain
    m_Layers.push_back(TileLayer("Ground Detail", 10, true));  // Layer 1: Ground details
    m_Layers.push_back(TileLayer("Objects", 20, true));        // Layer 2: Background objects
    m_Layers.push_back(TileLayer("Objects2", 30, true));       // Layer 3: More background objects
    m_Layers.push_back(TileLayer("Objects3", 40, true));       // Layer 4: Extra background objects

    // Foreground layers (rendered after player for depth)
    m_Layers.push_back(TileLayer("Foreground", 100, false));   // Layer 5: Foreground objects
    m_Layers.push_back(TileLayer("Foreground2", 110, false));  // Layer 6: More foreground
    m_Layers.push_back(TileLayer("Overlay", 120, false));      // Layer 7: Top overlay
    m_Layers.push_back(TileLayer("Overlay2", 130, false));     // Layer 8: Extra top layer
    m_Layers.push_back(TileLayer("Overlay3", 140, false));     // Layer 9: Highest overlay

    // Resize all layer data arrays
    for (auto& layer : m_Layers)
    {
        layer.Resize(mapSize);
    }

    m_CollisionMap.Resize(m_MapWidth, m_MapHeight);
    m_NavigationMap.Resize(m_MapWidth, m_MapHeight);
    m_CornerCutBlocked.assign(mapSize, 0);  // All corners allow cutting by default

    // Initialize animation map
    m_TileAnimationMap.assign(mapSize, -1);
    m_AnimationTime = 0.0f;

    m_FloodFillProcessed.assign(mapSize, false);

    InvalidateStructureBoundsCache();

    if (generateMap && m_TilesetWidth > 0 && m_TilesetHeight > 0)
        GenerateDefaultMap();
}

void Tilemap::SetTileCollision(int x, int y, bool hasCollision)
{
    m_CollisionMap.SetCollision(x, y, hasCollision);
}

bool Tilemap::GetTileCollision(int x, int y) const
{
    return m_CollisionMap.HasCollision(x, y);
}

void Tilemap::SetCornerCutBlocked(int x, int y, Corner corner, bool blocked)
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return;
    size_t idx = FlatIndex(x, y);
    if (idx >= m_CornerCutBlocked.size())
        return;

    uint8_t bit = 1 << static_cast<uint8_t>(corner);
    if (blocked)
        m_CornerCutBlocked[idx] |= bit;
    else
        m_CornerCutBlocked[idx] &= ~bit;
}

bool Tilemap::IsCornerCutBlocked(int x, int y, Corner corner) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return false;
    size_t idx = FlatIndex(x, y);
    if (idx >= m_CornerCutBlocked.size())
        return false;

    uint8_t bit = 1 << static_cast<uint8_t>(corner);
    return (m_CornerCutBlocked[idx] & bit) != 0;
}

void Tilemap::SetNavigation(int x, int y, bool walkable)
{
    m_NavigationMap.SetNavigation(x, y, walkable);
}

bool Tilemap::GetNavigation(int x, int y) const
{
    return m_NavigationMap.GetNavigation(x, y);
}

// Return whether the given tile id is fully transparent, using the precomputed
// transparency cache when available and falling back to a live pixel scan of the
// tileset otherwise. Unknown or unloaded tiles are treated as transparent.
bool Tilemap::IsTileTransparent(int tileID) const
{
    // Use cached result if available (massive performance improvement)
    if (m_TransparencyCacheBuilt && tileID >= 0 &&
        tileID < static_cast<int>(m_TileTransparencyCache.size()))
    {
        return m_TileTransparencyCache[tileID];
    }

    // Fallback to pixel scanning if cache not available
    if (!m_TilesetData || tileID < 0 || m_TilesetChannels == 0)
    {
        return true;  // Treat as transparent if we can't check
    }

    int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
    int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

    if (tilesetX + m_TileWidth > m_TilesetDataWidth ||
        tilesetY + m_TileHeight > m_TilesetDataHeight)
    {
        return true;
    }

    for (int y = 0; y < m_TileHeight; ++y)
    {
        for (int x = 0; x < m_TileWidth; ++x)
        {
            int px = tilesetX + x;
            int py = tilesetY + y;
            if (px >= m_TilesetDataWidth || py >= m_TilesetDataHeight)
                continue;

            int index = (py * m_TilesetDataWidth + px) * m_TilesetChannels;
            if (index >= 0 && index < m_TilesetDataWidth * m_TilesetDataHeight * m_TilesetChannels)
            {
                if (m_TilesetChannels == 4)
                {
                    if (m_TilesetData[index + 3] > 0)
                        return false;
                }
                else if (m_TilesetChannels == 3)
                {
                    unsigned char r = m_TilesetData[index];
                    unsigned char g = m_TilesetData[index + 1];
                    unsigned char b = m_TilesetData[index + 2];
                    if (!(r == 0 && g == 0 && b == 0) && !(r == 255 && g == 255 && b == 255))
                        return false;
                }
            }
        }
    }
    return true;
}

int Tilemap::GetElevation(int x, int y) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return 0;

    size_t index = FlatIndex(x, y);
    if (index >= m_Elevation.size())
        return 0;

    return m_Elevation[index];
}

void Tilemap::SetElevation(int x, int y, int elevation)
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return;

    size_t index = FlatIndex(x, y);
    if (index >= m_Elevation.size())
        return;

    if (m_Elevation[index] != elevation)
    {
        m_Elevation[index] = elevation;
        m_ElevationRegionIdsDirty = true;
    }
}

void Tilemap::RebuildElevationRegionIds() const
{
    const size_t mapSize = MapCellCount();
    m_ElevationRegionIds.assign(mapSize, -1);

    int nextRegionId = 0;
    std::vector<size_t> pending;
    for (size_t seed = 0; seed < mapSize; ++seed)
    {
        if (seed >= m_Elevation.size() || m_Elevation[seed] == 0 || m_ElevationRegionIds[seed] >= 0)
        {
            continue;
        }

        m_ElevationRegionIds[seed] = nextRegionId;
        pending.push_back(seed);
        while (!pending.empty())
        {
            const size_t current = pending.back();
            pending.pop_back();

            const int x = static_cast<int>(current % static_cast<size_t>(m_MapWidth));
            const int y = static_cast<int>(current / static_cast<size_t>(m_MapWidth));
            const int neighborX[] = {x - 1, x + 1, x, x};
            const int neighborY[] = {y, y, y - 1, y + 1};
            for (int neighbor = 0; neighbor < 4; ++neighbor)
            {
                const int nx = neighborX[neighbor];
                const int ny = neighborY[neighbor];
                if (nx < 0 || nx >= m_MapWidth || ny < 0 || ny >= m_MapHeight)
                {
                    continue;
                }

                const size_t neighborIndex = FlatIndex(nx, ny);
                if (neighborIndex >= m_Elevation.size() || m_Elevation[neighborIndex] == 0 ||
                    m_ElevationRegionIds[neighborIndex] >= 0)
                {
                    continue;
                }

                m_ElevationRegionIds[neighborIndex] = nextRegionId;
                pending.push_back(neighborIndex);
            }
        }
        ++nextRegionId;
    }

    m_ElevationRegionIdsDirty = false;
}

int Tilemap::GetElevationRegionId(int x, int y) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
    {
        return -1;
    }
    if (m_ElevationRegionIdsDirty || m_ElevationRegionIds.size() != MapCellCount())
    {
        RebuildElevationRegionIds();
    }

    const size_t index = FlatIndex(x, y);
    return index < m_ElevationRegionIds.size() ? m_ElevationRegionIds[index] : -1;
}

int Tilemap::GetElevationRegionIdAtWorldPos(float worldX, float worldY) const
{
    int tileX = 0;
    int tileY = 0;
    WorldToTileCoord(worldX, worldY, tileX, tileY);
    return GetElevationRegionId(tileX, tileY);
}

float Tilemap::GetElevationAtWorldPos(float worldX, float worldY) const
{
    // Convert world position to tile coordinates
    // Note: Entity positions use "feet position" convention where Y is at the
    // bottom of the tile (y * tileHeight + tileHeight). Subtracting half a tile height
    // maps a feet position back to the tile it occupies.
    int tileX = static_cast<int>(std::floor(worldX / m_TileWidth));
    int tileY = static_cast<int>(std::floor((worldY - m_TileHeight * 0.5f) / m_TileHeight));

    // Return elevation of current tile
    return static_cast<float>(GetElevation(tileX, tileY));
}

// Decide which axis (X or Y) an elevated tile engages the player on: the long
// dimension of the elevated region, so ramps and bridges block along their run yet
// let entities pass under/over when crossing them. See the inline steps below for
// the gradient-first / extent-scan fallback logic.
ElevationAxis Tilemap::GetElevationAxisAt(int x, int y) const
{
    int z = GetElevation(x, y);
    if (z == 0)
    {
        // Ground tiles always engage from any direction so the player can
        // step back onto the ground when leaving an elevated region.
        return ElevationAxis::None;
    }

    int eE = GetElevation(x + 1, y);
    int eW = GetElevation(x - 1, y);
    int eN = GetElevation(x, y - 1);
    int eS = GetElevation(x, y + 1);

    // Primary signal: gradient direction. A ramp transitioning E-W has a
    // strong X gradient and zero (or weak) Y gradient even when the ramp
    // is multiple tiles tall - same-elevation neighbors on Y do not fool
    // the gradient the way they fool a continuity-first rule.
    int dx = std::abs(eE - eW);
    int dy = std::abs(eN - eS);
    if (dx > dy)
    {
        return ElevationAxis::X;
    }
    if (dy > dx)
    {
        return ElevationAxis::Y;
    }

    // Gradient tied (deck interior, isolated platform, or symmetric cross).
    // Fall back to a bounded extent scan: walk outward in each cardinal
    // direction and count contiguous elevated cells. The axis with greater
    // span is the long dimension of the elevated region - that's the bridge
    // axis and so the engagement axis.
    constexpr int SCAN_LIMIT = 8;
    auto scanDir = [&](int sx, int sy)
    {
        int count = 0;
        for (int s = 1; s <= SCAN_LIMIT; ++s)
        {
            if (GetElevation(x + s * sx, y + s * sy) > 0)
            {
                ++count;
            }
            else
            {
                break;
            }
        }
        return count;
    };
    int spanX = scanDir(1, 0) + scanDir(-1, 0);
    int spanY = scanDir(0, 1) + scanDir(0, -1);
    if (spanX > spanY)
    {
        return ElevationAxis::X;
    }
    if (spanY > spanX)
    {
        return ElevationAxis::Y;
    }

    // Truly ambiguous (e.g. uniform-elevation 3x3 isolated platform):
    // default to X.
    return ElevationAxis::X;
}

bool Tilemap::IsStructureTile(int x, int y, int layer) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return false;

    // Convert 1-indexed layer to 0-indexed dynamic layer
    size_t layerIdx = static_cast<size_t>(layer - 1);
    if (layerIdx >= m_Layers.size())
        return false;

    size_t index = FlatIndex(x, y);
    return m_Layers[layerIdx].stance[index] == TileStance::Structure;
}

// Flood-fill from (tileX, tileY) across all layers to find the tile-space bounds of
// the connected structure region containing it (4-connectivity; a tile counts if any
// layer gives it TileStance::Structure). Writes the bounds to the out params and
// returns true, or returns false if the seed tile is out of range or not part of
// any structure region.
bool Tilemap::FindNoProjectionStructureBounds(
    int tileX, int tileY, int& outMinX, int& outMaxX, int& outMinY, int& outMaxY) const
{
    if (tileX < 0 || tileX >= m_MapWidth || tileY < 0 || tileY >= m_MapHeight)
        return false;

    // Check if this tile is a structure on any layer
    size_t idx = FlatIndex(tileX, tileY);
    bool hasStructure = false;
    for (size_t li = 0; li < m_Layers.size(); ++li)
    {
        if (idx < m_Layers[li].stance.size() && m_Layers[li].stance[idx] == TileStance::Structure)
        {
            hasStructure = true;
            break;
        }
    }
    if (!hasStructure)
        return false;

    // Flood-fill to find all connected structure tiles (same as RenderLayersNoProjection).
    // Reuse the member buffer to avoid per-call allocation on large maps.
    const size_t mapSize = static_cast<size_t>(m_MapWidth) * static_cast<size_t>(m_MapHeight);
    if (m_FloodFillProcessed.size() != mapSize)
    {
        m_FloodFillProcessed.assign(mapSize, false);
    }
    else
    {
        std::fill(m_FloodFillProcessed.begin(), m_FloodFillProcessed.end(), false);
    }
    auto& processed = m_FloodFillProcessed;
    std::vector<std::pair<int, int>> stack;
    stack.push_back({tileX, tileY});

    outMinX = tileX;
    outMaxX = tileX;
    outMinY = tileY;
    outMaxY = tileY;

    while (!stack.empty())
    {
        auto [cx, cy] = stack.back();
        stack.pop_back();

        if (cx < 0 || cx >= m_MapWidth || cy < 0 || cy >= m_MapHeight)
            continue;

        size_t cIdx = FlatIndex(cx, cy);
        if (processed[cIdx])
            continue;

        // Check if this tile is a structure on any layer
        bool isStructure = false;
        for (size_t li = 0; li < m_Layers.size(); ++li)
        {
            if (cIdx < m_Layers[li].stance.size() &&
                m_Layers[li].stance[cIdx] == TileStance::Structure)
            {
                isStructure = true;
                break;
            }
        }
        if (!isStructure)
            continue;

        processed[cIdx] = true;

        outMinX = std::min(outMinX, cx);
        outMaxX = std::max(outMaxX, cx);
        outMinY = std::min(outMinY, cy);
        outMaxY = std::max(outMaxY, cy);

        // 4-way connectivity
        stack.push_back({cx - 1, cy});
        stack.push_back({cx + 1, cy});
        stack.push_back({cx, cy - 1});
        stack.push_back({cx, cy + 1});
    }

    return true;
}

// Map a world-space point onto the mesh of the upright structure beneath it,
// writing the on-screen position to outScreenPos. Finds the owning structure
// (searching a few rows down from the point), then interpolates across the
// structure's column edges at the point's fractional position. Returns false
// when there is no structure under the point. Lets particles stay locked to a
// building's face: the structure stands on its anchor base, so a point inside
// its footprint lands on the extruded face rather than on the ground below.
bool Tilemap::ProjectNoProjectionStructurePoint(const glm::vec2& worldPos,
                                                const glm::vec2& cameraPos,
                                                glm::vec2& outScreenPos) const
{
    if (m_TileWidth <= 0 || m_TileHeight <= 0)
        return false;

    int queryTileX = static_cast<int>(std::floor(worldPos.x / static_cast<float>(m_TileWidth)));
    int queryTileY = static_cast<int>(std::floor(worldPos.y / static_cast<float>(m_TileHeight)));

    if (queryTileX < 0 || queryTileX >= m_MapWidth)
        return false;

    struct RowCandidate
    {
        bool valid = false;
        size_t layerIdx = 0;
        int structId = -1;
        int tileY = 0;
    };

    auto findCandidateInRow = [&](int tileY) -> RowCandidate
    {
        RowCandidate best;
        if (tileY < 0 || tileY >= m_MapHeight)
            return best;

        size_t idx = FlatIndex(queryTileX, tileY);
        bool haveBest = false;
        int bestRenderOrder = 0;

        for (size_t layerIdx = 0; layerIdx < m_Layers.size(); ++layerIdx)
        {
            const TileLayer& layer = m_Layers[layerIdx];
            if (idx >= layer.stance.size() || layer.stance[idx] != TileStance::Structure)
                continue;
            if (idx >= layer.structureId.size())
                continue;

            int sid = layer.structureId[idx];
            if (sid < 0 || sid >= static_cast<int>(m_NoProjectionStructures.size()))
                continue;

            if (!haveBest || layer.renderOrder > bestRenderOrder)
            {
                haveBest = true;
                bestRenderOrder = layer.renderOrder;
                best.valid = true;
                best.layerIdx = layerIdx;
                best.structId = sid;
                best.tileY = tileY;
            }
        }

        return best;
    };

    constexpr int SEARCH_DOWN_TILES = 8;
    RowCandidate candidate;
    for (int dy = 0; dy <= SEARCH_DOWN_TILES; ++dy)
    {
        int testY = queryTileY + dy;
        candidate = findCandidateInRow(testY);
        if (candidate.valid)
            break;
    }

    if (!candidate.valid)
        return false;

    int structId = candidate.structId;

    // Look up cached structure bounds (O(1) instead of full-map scan)
    const StructureBounds* bounds = GetCachedStructureBounds(candidate.layerIdx, structId);
    if (!bounds)
        return false;

    int minX = bounds->minX;
    int maxX = bounds->maxX;
    int minY = bounds->minY;
    int maxY = bounds->maxY;

    int structureWidthTiles = maxX - minX + 1;
    if (structureWidthTiles < 1)
        return false;

    float tileWf = static_cast<float>(m_TileWidth);
    float tileHf = static_cast<float>(m_TileHeight);
    float localXTiles = (worldPos.x / tileWf) - static_cast<float>(minX);
    float widthTilesF = static_cast<float>(structureWidthTiles);

    if (localXTiles < 0.0f || localXTiles > widthTilesF)
        return false;

    localXTiles = std::max(0.0f, std::min(localXTiles, widthTilesF - 0.0001f));
    int tileCol = static_cast<int>(std::floor(localXTiles));
    float fracX = localXTiles - static_cast<float>(tileCol);

    int leftEdgeIndex = tileCol;
    int rightEdgeIndex = tileCol + 1;
    if (leftEdgeIndex < 0 || rightEdgeIndex > structureWidthTiles)
        return false;

    const NoProjectionStructure& structDef = m_NoProjectionStructures[structId];
    float anchorMinX = std::min(structDef.leftAnchor.x, structDef.rightAnchor.x);
    float anchorMaxX = std::max(structDef.leftAnchor.x, structDef.rightAnchor.x);
    float bottomWorldY = std::max(structDef.leftAnchor.y, structDef.rightAnchor.y);
    float bottomScreenY = bottomWorldY - cameraPos.y + 1.0f;
    float anchorMinScreenX = anchorMinX - cameraPos.x;
    float anchorMaxScreenX = anchorMaxX - cameraPos.x;

    float worldTileY = worldPos.y / tileHf;

    glm::vec2 leftPoint = ComputeEdgePoint(anchorMinScreenX,
                                           anchorMaxScreenX,
                                           bottomScreenY,
                                           minY,
                                           maxY,
                                           m_TileHeight,
                                           structureWidthTiles,
                                           leftEdgeIndex,
                                           worldTileY);

    glm::vec2 rightPoint = ComputeEdgePoint(anchorMinScreenX,
                                            anchorMaxScreenX,
                                            bottomScreenY,
                                            minY,
                                            maxY,
                                            m_TileHeight,
                                            structureWidthTiles,
                                            rightEdgeIndex,
                                            worldTileY);

    outScreenPos = leftPoint + (rightPoint - leftPoint) * fracX;
    return true;
}

int Tilemap::AddNoProjectionStructure(glm::vec2 leftAnchor,
                                      glm::vec2 rightAnchor,
                                      const std::string& name)
{
    int id = static_cast<int>(m_NoProjectionStructures.size());
    m_NoProjectionStructures.emplace_back(id, leftAnchor, rightAnchor, name);
    InvalidateStructureBoundsCache();
    return id;
}

const NoProjectionStructure* Tilemap::GetNoProjectionStructure(int id) const
{
    if (id < 0 || id >= static_cast<int>(m_NoProjectionStructures.size()))
        return nullptr;
    return &m_NoProjectionStructures[id];
}

void Tilemap::RemoveNoProjectionStructure(int id)
{
    if (id < 0 || id >= static_cast<int>(m_NoProjectionStructures.size()))
        return;

    // Clear structureId from all tiles that referenced this structure
    for (auto& layer : m_Layers)
    {
        for (size_t i = 0; i < layer.structureId.size(); ++i)
        {
            if (layer.structureId[i] == id)
                layer.structureId[i] = -1;
            else if (layer.structureId[i] > id)
                layer.structureId[i]--;  // Shift down IDs above removed one
        }
    }

    // Remove the structure
    m_NoProjectionStructures.erase(m_NoProjectionStructures.begin() + id);

    // Update IDs in remaining structures
    for (size_t i = static_cast<size_t>(id); i < m_NoProjectionStructures.size(); ++i)
    {
        m_NoProjectionStructures[i].id = static_cast<int>(i);
    }

    InvalidateStructureBoundsCache();
}

void Tilemap::InsertNoProjectionStructureAt(size_t idx, const NoProjectionStructure& structure)
{
    if (idx > m_NoProjectionStructures.size())
        return;

    // Shift up tile structureId references for structures currently at idx
    // and beyond (their indices are about to grow by one). Mirrors the
    // shift-down logic in RemoveNoProjectionStructure.
    for (auto& layer : m_Layers)
    {
        for (size_t i = 0; i < layer.structureId.size(); ++i)
        {
            if (layer.structureId[i] >= static_cast<int>(idx))
                layer.structureId[i]++;
        }
    }

    NoProjectionStructure copy = structure;
    copy.id = static_cast<int>(idx);
    m_NoProjectionStructures.insert(m_NoProjectionStructures.begin() + idx, copy);

    // Renumber ids from idx onward to match new positions.
    for (size_t i = idx; i < m_NoProjectionStructures.size(); ++i)
    {
        m_NoProjectionStructures[i].id = static_cast<int>(i);
    }

    InvalidateStructureBoundsCache();
}

int Tilemap::GetTileStructureId(int x, int y, int layer) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return -1;

    size_t layerIdx = static_cast<size_t>(layer - 1);
    if (layerIdx >= m_Layers.size())
        return -1;

    size_t index = FlatIndex(x, y);
    if (index >= m_Layers[layerIdx].structureId.size())
        return -1;

    return m_Layers[layerIdx].structureId[index];
}

void Tilemap::SetTileStructureId(int x, int y, int layer, int structId)
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return;

    size_t layerIdx = static_cast<size_t>(layer - 1);
    if (layerIdx >= m_Layers.size())
        return;

    size_t index = FlatIndex(x, y);
    if (index >= m_Layers[layerIdx].structureId.size())
        return;

    int oldStructId = m_Layers[layerIdx].structureId[index];
    m_Layers[layerIdx].structureId[index] = structId;
    InvalidateStructureBoundsForTile(layerIdx, x, y, oldStructId, structId);
}

bool Tilemap::IsDepthSortedTile(int x, int y, size_t layerIdx) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight || layerIdx >= m_Layers.size())
    {
        return false;
    }

    const size_t index = FlatIndex(x, y);
    const TileLayer& layer = m_Layers[layerIdx];
    if (index >= layer.ySortPlus.size())
    {
        return false;
    }

    // The two ground/detail layers remain the implicit floor underneath elevated
    // artwork. Existing maps need no new authoring: elevation on an object or
    // foreground layer promotes that tile into the character depth queue.
    return layer.ySortPlus[index] || (layerIdx >= FIRST_OBJECT_LAYER && GetElevation(x, y) != 0);
}

// Rebuild and return the cached list of visible tiles that participate in world
// depth sorting. Explicit Y-sort column stacks retain their legacy bottom anchor;
// elevated surface artwork gets one entry per tile and carries the cell height.
const std::vector<Tilemap::DepthSortedTile>& Tilemap::GetVisibleDepthSortedTiles(
    glm::vec2 cullCam, glm::vec2 cullSize) const
{
    m_DepthSortedTilesCache.clear();

    float padX = STRUCTURE_SCAN_PADDING_TILES * static_cast<float>(m_TileWidth);
    float padY = STRUCTURE_SCAN_PADDING_TILES * static_cast<float>(m_TileHeight);
    glm::vec2 expandedCullCam(cullCam.x - padX, cullCam.y - padY);
    glm::vec2 expandedCullSize(cullSize.x + padX * 2.0f, cullSize.y + padY * 2.0f);

    int x0, y0, x1, y1;
    ComputeTileRange(m_MapWidth,
                     m_MapHeight,
                     m_TileWidth,
                     m_TileHeight,
                     expandedCullCam,
                     expandedCullSize,
                     x0,
                     y0,
                     x1,
                     y1);

    // Explicit Y-sort stacks retain their legacy vertical anchor. Inferred
    // elevated tiles remain individual queue entries; local region constraints
    // later place them relative only to actors under/on that footprint.
    auto isExplicitYSortTile = [this](int x, int y, size_t layerIdx) -> bool
    {
        if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        {
            return false;
        }
        if (layerIdx >= m_Layers.size())
        {
            return false;
        }
        const size_t index = FlatIndex(x, y);
        const TileLayer& layer = m_Layers[layerIdx];
        if (index >= layer.ySortPlus.size())
        {
            return false;
        }
        if (!layer.ySortPlus[index])
        {
            return false;
        }
        // Check for animation before checking base tile
        int tileID = layer.tiles[index];
        if (index < layer.animationMap.size())
        {
            int animId = layer.animationMap[index];
            if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
            {
                tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
            }
        }
        if (tileID < 0)
            return false;
        return true;
    };

    struct RenderSupport
    {
        SupportSurface surface{SupportSurface::Ground};
        int height{0};
        int regionId{-1};
        bool ambiguousRegion{false};
    };
    std::unordered_map<int64_t, RenderSupport> structureSupportCache;
    auto getStructureSupport = [this, &structureSupportCache](size_t layerIdx,
                                                              int structureId) -> RenderSupport
    {
        const int64_t key =
            (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(structureId);
        if (const auto cached = structureSupportCache.find(key);
            cached != structureSupportCache.end())
        {
            return cached->second;
        }

        RenderSupport support;
        const StructureBounds* bounds = GetCachedStructureBounds(layerIdx, structureId);
        if (bounds != nullptr)
        {
            const TileLayer& structureLayer = m_Layers[layerIdx];
            for (int sy = bounds->minY; sy <= bounds->maxY; ++sy)
            {
                for (int sx = bounds->minX; sx <= bounds->maxX; ++sx)
                {
                    const size_t structureIndex = FlatIndex(sx, sy);
                    if (structureIndex >= structureLayer.structureId.size() ||
                        structureLayer.structureId[structureIndex] != structureId)
                    {
                        continue;
                    }

                    const int height = GetElevation(sx, sy);
                    if (std::abs(height) > std::abs(support.height))
                    {
                        support.height = height;
                    }

                    const int regionId = GetElevationRegionId(sx, sy);
                    if (regionId >= 0)
                    {
                        if (support.regionId < 0)
                        {
                            support.regionId = regionId;
                        }
                        else if (support.regionId != regionId)
                        {
                            // One visual structure spanning unrelated elevation
                            // footprints is ambiguous; do not apply local plane
                            // constraints to its overhanging cells.
                            support.ambiguousRegion = true;
                        }
                    }
                }
            }
        }

        if (support.height != 0)
        {
            support.surface = SupportSurface::Elevation;
        }
        if (support.ambiguousRegion)
        {
            support.regionId = -1;
        }
        structureSupportCache.emplace(key, support);
        return support;
    };

    std::unordered_map<int64_t, RenderSupport> ySortComponentSupportCache;
    auto getYSortComponentSupport = [this, &isExplicitYSortTile, &ySortComponentSupportCache](
                                        int seedX, int seedY, size_t layerIdx) -> RenderSupport
    {
        const size_t seedIndex = FlatIndex(seedX, seedY);
        const int64_t seedKey =
            (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(seedIndex);
        if (const auto cached = ySortComponentSupportCache.find(seedKey);
            cached != ySortComponentSupportCache.end())
        {
            return cached->second;
        }

        RenderSupport support;
        std::vector<size_t> pending{seedIndex};
        std::vector<size_t> component;
        std::unordered_set<size_t> visited;
        visited.insert(seedIndex);
        while (!pending.empty())
        {
            const size_t current = pending.back();
            pending.pop_back();
            component.push_back(current);

            const int x = static_cast<int>(current % static_cast<size_t>(m_MapWidth));
            const int y = static_cast<int>(current / static_cast<size_t>(m_MapWidth));
            const int height = GetElevation(x, y);
            if (std::abs(height) > std::abs(support.height))
            {
                support.height = height;
            }

            const int regionId = GetElevationRegionId(x, y);
            if (regionId >= 0)
            {
                if (support.regionId < 0)
                {
                    support.regionId = regionId;
                }
                else if (support.regionId != regionId)
                {
                    support.ambiguousRegion = true;
                }
            }

            const int neighborX[] = {x - 1, x + 1, x, x};
            const int neighborY[] = {y, y, y - 1, y + 1};
            for (int neighbor = 0; neighbor < 4; ++neighbor)
            {
                const int nx = neighborX[neighbor];
                const int ny = neighborY[neighbor];
                if (!isExplicitYSortTile(nx, ny, layerIdx))
                {
                    continue;
                }

                const size_t neighborIndex = FlatIndex(nx, ny);
                if (visited.insert(neighborIndex).second)
                {
                    pending.push_back(neighborIndex);
                }
            }
        }

        if (support.height != 0)
        {
            support.surface = SupportSurface::Elevation;
        }
        if (support.ambiguousRegion)
        {
            support.regionId = -1;
        }
        for (size_t componentIndex : component)
        {
            const int64_t componentKey =
                (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(componentIndex);
            ySortComponentSupportCache.emplace(componentKey, support);
        }
        return support;
    };

    // Preserve authored layer render order for equal-depth tile ties.
    for (size_t layerIdx : GetLayerRenderOrder())
    {
        const TileLayer& layer = m_Layers[layerIdx];

        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                size_t index = FlatIndex(x, y);
                if (index >= layer.ySortPlus.size() || !IsDepthSortedTile(x, y, layerIdx))
                {
                    continue;
                }

                // Check for animation before checking base tile
                int tileID = layer.tiles[index];
                if (index < layer.animationMap.size())
                {
                    int animId = layer.animationMap[index];
                    if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
                    {
                        tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
                    }
                }
                if (tileID < 0)
                    continue;

                // Find the bottom of an explicitly Y-sorted vertical object.
                // Inferred elevated surface cells keep their own physical anchor.
                int bottomY = y;
                if (layer.ySortPlus[index])
                {
                    while (isExplicitYSortTile(x, bottomY + 1, layerIdx))
                    {
                        bottomY++;
                    }
                }

                // A tall visual structure often extends outside its walkable
                // elevation cells. Inherit support from the whole authored
                // structure (or, without a structure id, its vertical Y-sort
                // stack) so underpass constraints can identify the same local
                // elevation footprint without changing authored Y-sort roles.
                int inheritedHeight = GetElevation(x, bottomY);
                int inheritedRegionId = GetElevationRegionId(x, bottomY);
                bool ambiguousRegion = false;
                if (layer.ySortPlus[index])
                {
                    const RenderSupport componentSupport = getYSortComponentSupport(x, y, layerIdx);
                    if (std::abs(componentSupport.height) > std::abs(inheritedHeight))
                    {
                        inheritedHeight = componentSupport.height;
                    }
                    if (inheritedRegionId < 0)
                    {
                        inheritedRegionId = componentSupport.regionId;
                    }
                    else if (componentSupport.regionId >= 0 &&
                             inheritedRegionId != componentSupport.regionId)
                    {
                        ambiguousRegion = true;
                    }

                    int topY = y;
                    while (isExplicitYSortTile(x, topY - 1, layerIdx))
                    {
                        topY--;
                    }
                    for (int stackY = topY; stackY <= bottomY; ++stackY)
                    {
                        const int stackHeight = GetElevation(x, stackY);
                        if (std::abs(stackHeight) > std::abs(inheritedHeight))
                        {
                            inheritedHeight = stackHeight;
                        }

                        const int stackRegionId = GetElevationRegionId(x, stackY);
                        if (stackRegionId >= 0)
                        {
                            if (inheritedRegionId < 0)
                            {
                                inheritedRegionId = stackRegionId;
                            }
                            else if (inheritedRegionId != stackRegionId)
                            {
                                ambiguousRegion = true;
                            }
                        }
                    }
                }

                const int structureId =
                    index < layer.structureId.size() ? layer.structureId[index] : -1;
                if (structureId >= 0)
                {
                    const RenderSupport structureSupport =
                        getStructureSupport(layerIdx, structureId);
                    if (structureSupport.surface == SupportSurface::Elevation)
                    {
                        inheritedHeight = structureSupport.height;
                        if (inheritedRegionId < 0)
                        {
                            inheritedRegionId = structureSupport.regionId;
                        }
                        else if (structureSupport.regionId >= 0 &&
                                 inheritedRegionId != structureSupport.regionId)
                        {
                            ambiguousRegion = true;
                        }
                    }
                }
                if (ambiguousRegion)
                {
                    inheritedRegionId = -1;
                }

                DepthSortedTile tile;
                tile.x = x;
                tile.y = y;
                tile.layer = static_cast<int>(layerIdx);
                tile.anchorY = static_cast<float>((bottomY + 1) * m_TileHeight);
                tile.supportHeight = static_cast<float>(inheritedHeight);
                tile.supportSurface =
                    tile.supportHeight == 0.0f ? SupportSurface::Ground : SupportSurface::Elevation;
                tile.surfaceRegionId = inheritedRegionId;
                tile.authoredYSort = layer.ySortPlus[index];
                tile.isBackground = layer.isBackground;
                tile.isStructure = layer.stance[index] == TileStance::Structure;
                const size_t bottomIndex = FlatIndex(x, bottomY);
                tile.ySortMinus = layer.ySortMinus[bottomIndex];
                m_DepthSortedTilesCache.push_back(tile);
            }
        }
    }

    return m_DepthSortedTilesCache;
}

// Draw a single tile at (x, y) on the given layer. Handles animation-frame lookup
// and transparency skipping. Upright tiles draw exactly like flat ones here: the
// stance only changes draw order in this pipeline, never geometry.
void Tilemap::RenderSingleTile(IRenderer& renderer, int x, int y, int layer, glm::vec2 cameraPos)
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return;

    size_t layerIdx = static_cast<size_t>(layer);
    if (layerIdx >= m_Layers.size())
        return;

    size_t index = FlatIndex(x, y);
    const TileLayer& tileLayer = m_Layers[layerIdx];

    if (index >= tileLayer.tiles.size())
        return;

    int tileID = tileLayer.tiles[index];
    float rotation = tileLayer.rotation[index];
    bool tileFlipX = (index < tileLayer.flipX.size()) ? tileLayer.flipX[index] : false;
    bool tileFlipY = (index < tileLayer.flipY.size()) ? tileLayer.flipY[index] : false;

    // Check for animated tile before skip check
    if (index < tileLayer.animationMap.size())
    {
        int animId = tileLayer.animationMap[index];
        if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
        {
            tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
        }
    }

    if (tileID < 0)
        return;

    if (IsTileTransparent(tileID))
        return;

    int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
    int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;
    glm::vec2 texCoord(static_cast<float>(tilesetX), static_cast<float>(tilesetY));
    glm::vec2 texSize(static_cast<float>(m_TileWidth), static_cast<float>(m_TileHeight));
    bool flipY = renderer.RequiresYFlip();

    float worldX = static_cast<float>(x * m_TileWidth);
    float worldY = static_cast<float>(y * m_TileHeight);
    glm::vec2 screenPos(worldX - cameraPos.x, worldY - cameraPos.y);
    glm::vec2 renderSize(static_cast<float>(m_TileWidth), static_cast<float>(m_TileHeight));

    renderer.DrawSpriteRegion(m_TilesetTexture,
                              screenPos,
                              renderSize,
                              texCoord,
                              texSize,
                              rotation,
                              glm::vec3(1.0f),
                              flipY,
                              tileFlipX,
                              tileFlipY);
}

TileLayer& Tilemap::GetLayer(size_t index)
{
    if (index >= m_Layers.size())
    {
        throw std::out_of_range("Layer index out of range");
    }
    return m_Layers[index];
}

const TileLayer& Tilemap::GetLayer(size_t index) const
{
    if (index >= m_Layers.size())
    {
        throw std::out_of_range("Layer index out of range");
    }
    return m_Layers[index];
}

// Per-cell layer field accessors. Each get/set delegates to the GetLayerField /
// SetLayerField templates (declared in Tilemap.hpp), which bounds-check (x, y,
// layer) and index into the named TileLayer array. Rotation is special-cased to
// normalize the angle into [0, 360).
int Tilemap::GetLayerTile(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::tiles>(x, y, layer);
}

void Tilemap::SetLayerTile(int x, int y, size_t layer, int tileID)
{
    SetLayerField<&TileLayer::tiles>(x, y, layer, tileID);
}

float Tilemap::GetLayerRotation(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::rotation>(x, y, layer);
}

void Tilemap::SetLayerRotation(int x, int y, size_t layer, float rotation)
{
    if (layer >= m_Layers.size() || x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return;
    // Normalize rotation to [0, 360) range
    rotation = std::fmod(rotation, 360.0f);
    if (rotation < 0.0f)
        rotation += 360.0f;
    (m_Layers[layer].rotation)[FlatIndex(x, y)] = rotation;
}

TileStance Tilemap::GetLayerStance(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::stance>(x, y, layer);
}

void Tilemap::SetLayerStance(int x, int y, size_t layer, TileStance stance)
{
    SetLayerField<&TileLayer::stance>(x, y, layer, stance);
}

ElevationRole Tilemap::GetLayerElevationRole(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::elevationRole>(x, y, layer);
}

void Tilemap::SetLayerElevationRole(int x, int y, size_t layer, ElevationRole role)
{
    SetLayerField<&TileLayer::elevationRole>(x, y, layer, role);
}

bool Tilemap::GetLayerFlipX(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::flipX>(x, y, layer);
}

void Tilemap::SetLayerFlipX(int x, int y, size_t layer, bool flipX)
{
    SetLayerField<&TileLayer::flipX>(x, y, layer, flipX);
}

bool Tilemap::GetLayerFlipY(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::flipY>(x, y, layer);
}

void Tilemap::SetLayerFlipY(int x, int y, size_t layer, bool flipY)
{
    SetLayerField<&TileLayer::flipY>(x, y, layer, flipY);
}

bool Tilemap::GetLayerYSortPlus(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::ySortPlus>(x, y, layer);
}

void Tilemap::SetLayerYSortPlus(int x, int y, size_t layer, bool ySortPlus)
{
    SetLayerField<&TileLayer::ySortPlus>(x, y, layer, ySortPlus);
}

bool Tilemap::GetLayerYSortMinus(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::ySortMinus>(x, y, layer);
}

void Tilemap::SetLayerYSortMinus(int x, int y, size_t layer, bool ySortMinus)
{
    SetLayerField<&TileLayer::ySortMinus>(x, y, layer, ySortMinus);
}

// Return layer indices sorted ascending by their renderOrder field, i.e. the order
// the layers should be drawn (lowest renderOrder first).
std::vector<size_t> Tilemap::GetLayerRenderOrder() const
{
    std::vector<size_t> indices(m_Layers.size());
    for (size_t i = 0; i < m_Layers.size(); ++i)
    {
        indices[i] = i;
    }
    std::sort(indices.begin(),
              indices.end(),
              [this](size_t a, size_t b)
              { return m_Layers[a].renderOrder < m_Layers[b].renderOrder; });
    return indices;
}

int Tilemap::FindStructureBaseRow(const TileLayer& layer, int tileX, int tileY) const
{
    int baseRow = tileY;
    while (baseRow + 1 < m_MapHeight)
    {
        const size_t belowIdx = static_cast<size_t>((baseRow + 1) * m_MapWidth + tileX);
        if (layer.tiles[belowIdx] < 0)
        {
            break;
        }
        // Only Structure artwork continues the run. A fence post standing
        // directly south of a building is a separate prop, not that building's
        // ground floor.
        if (!tileRole::StacksVertically(layer.stance[belowIdx]))
        {
            break;
        }
        ++baseRow;
    }
    return baseRow;
}

void Tilemap::FindStructureRunColumns(
    const TileLayer& layer, int tileX, int tileY, int& outMinX, int& outMaxX) const
{
    const auto matchesAt = [&](int x)
    {
        const size_t idx = static_cast<size_t>(tileY * m_MapWidth + x);
        if (layer.tiles[idx] < 0)
        {
            return false;
        }
        // Only Structure cells continue the run, so a Wall or a Prop standing
        // beside a building is never absorbed into it and frozen with its pivot.
        return tileRole::StacksVertically(layer.stance[idx]);
    };

    outMinX = tileX;
    while (outMinX - 1 >= 0 && matchesAt(outMinX - 1))
    {
        --outMinX;
    }

    outMaxX = tileX;
    while (outMaxX + 1 < m_MapWidth && matchesAt(outMaxX + 1))
    {
        ++outMaxX;
    }
}

Tilemap::SurfaceSlope Tilemap::ResolveSurfaceSlope(const TileLayer& layer,
                                                   int tileX,
                                                   int tileY) const
{
    const size_t idx =
        static_cast<size_t>(tileY) * static_cast<size_t>(m_MapWidth) + static_cast<size_t>(tileX);
    const ElevationRole role = layer.elevationRole[idx];
    const int elevation = GetElevation(tileX, tileY);

    if (role != ElevationRole::Ramp)
    {
        const float height = elevationRole::SurfaceHeight(elevation, role);
        return {height, height, false};
    }

    const bool alongZ = GetElevationAxisAt(tileX, tileY) == ElevationAxis::Y;
    const int stepX = alongZ ? 0 : 1;
    const int stepY = alongZ ? 1 : 0;

    // Off-map reads as bare ground, so a ramp at the map edge runs down to 0
    // rather than reading past the buffer.
    const auto neighbourAt = [&](int nx, int ny)
    {
        elevationRole::NeighbourSurface neighbour{};
        if (nx >= 0 && nx < m_MapWidth && ny >= 0 && ny < m_MapHeight)
        {
            neighbour.elevation = GetElevation(nx, ny);
            neighbour.role =
                layer.elevationRole[static_cast<size_t>(ny) * static_cast<size_t>(m_MapWidth) +
                                    static_cast<size_t>(nx)];
        }
        return neighbour;
    };

    return {elevationRole::EdgeHeight(elevation, neighbourAt(tileX - stepX, tileY - stepY)),
            elevationRole::EdgeHeight(elevation, neighbourAt(tileX + stepX, tileY + stepY)),
            alongZ};
}

void Tilemap::RenderWorld3D(IRenderer& renderer, const cameraRig::RigParams& rig)
{
    const std::vector<size_t> order = GetLayerRenderOrder();
    if (order.empty())
    {
        return;
    }

    // The visible ground area is a rotated trapezoid, so its axis-aligned bounds
    // come from the camera itself rather than from a viewport rectangle. When
    // the horizon is on screen the bounds are clamped rather than infinite, and
    // ComputeTileRange clips them to the map either way.
    const cameraRig::GroundBounds bounds = cameraRig::GroundFootprintAabb(rig);
    const glm::vec2 cullCam = bounds.min;
    const glm::vec2 cullSize = bounds.max - bounds.min;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    ComputeTileRange(
        m_MapWidth, m_MapHeight, m_TileWidth, m_TileHeight, cullCam, cullSize, x0, y0, x1, y1);

    // Guarded rather than an early return: this function's job is geometry, and
    // whether a tileset is loaded is the renderer's concern - DrawQuad3D
    // drops any quad whose texture is not ready. Returning here instead would
    // make the whole geometry path untestable without a GPU-loaded atlas.
    const int dataTilesPerRow = std::max(1, m_TilesetDataWidth / m_TileWidth);

    const float tileWf = static_cast<float>(m_TileWidth);
    const float tileHf = static_cast<float>(m_TileHeight);
    const glm::vec2 tileSize(tileWf, tileHf);
    const bool flipY = renderer.RequiresYFlip();
    const bool hasTransparencyCache = m_TransparencyCacheBuilt;
    const std::vector<uint8_t>& transparencyCache = m_TileTransparencyCache;
    const int transparencyCacheSize = static_cast<int>(transparencyCache.size());

    // Two orientations cover every upright tile, and the camera does not change
    // within a frame, so both are resolved once rather than per tile.
    //
    //  - pivot: turns toward the camera. Only safe for a pole - artwork with no
    //    real extent across the pivot, which spins in place and never leaves its
    //    anchor. Every TileStance::Prop uses it.
    //  - wall:  yaw locked to the grid, leaning only with pitch. Every surface
    //    uses it, because rotating a surface drags its far end off the spot it
    //    was authored on, and a run of individually turning cards fans open like
    //    venetian blinds. See tileRole::DampingFor.
    const billboard::Orientation pivotOrientation =
        billboard::Orient(rig.yawRadians, rig.pitchRadians, tileRole::DampingForWidth(1));
    const billboard::Orientation wallOrientation =
        billboard::Orient(rig.yawRadians, rig.pitchRadians, tileRole::DampingForWidth(2));

    // The tile range above is an axis-aligned box around a rotated trapezoid, so
    // at a 45 degree yaw a large fraction of it is off screen. A per-tile sphere
    // test against the real frustum removes those corners. The sphere is sized
    // to cover an upright tile as well as a flat one, since upright artwork
    // extends a full tile above the ground.
    const frustum::Frustum viewFrustum =
        frustum::FromViewProjection(cameraRig::BuildViewProjection(rig));
    const float tileRadius = std::sqrt(tileWf * tileWf + tileHf * tileHf);

    // Upright artwork is drawn above its base row, so a structure whose base is
    // inside the visible range can have its upper tiles on map rows north of it.
    // Scanning a margin further north keeps tall buildings from losing their top
    // as the camera moves. Flat tiles in that margin are skipped, so the extra
    // rows only cost the upright test.
    const int yScanStart = std::max(0, y0 - UPRIGHT_SCAN_MARGIN_TILES);

    // No seam inflation here: corners derived from the tile grid are
    // bit-identical between neighbours, so the shared edges close on their own.
    //
    // two passes, and the split is what stops rotated tiles flickering.
    //
    // Ground artwork is one flat sheet: every layer of it sits at height 0, so
    // stacked tiles are exactly COPLANAR. While tiles were axis-aligned that was
    // harmless - identical geometry rasterizes to bit-identical depth, and
    // GL_LEQUAL then lets the later draw win deterministically, which is exactly
    // the layer order the map asks for. Rotation defeats it: a rotated quad pokes out
    // past its own cell into its neighbours', and those overlaps are coplanar
    // surfaces with different geometry, so their interpolated depths differ only
    // by floating-point noise. Which one wins then varies per pixel and shifts
    // as the camera moves - z-fighting, seen as flicker.
    //
    // The fix is to stop asking depth a question it cannot answer. Ground draws
    // first with depth off entirely, ordered by layer exactly as the flat
    // pipeline ordered it - which is still correct for the coplanar case this
    // pass exists to fix: every layer of one cell sits at the same height. Since
    // ground can rise per cell (elevationRole::SurfaceHeight), a raised cell and
    // a lower one at a different cell are no longer necessarily coplanar, but
    // with depth off they are still only ever ordered by scan order rather than
    // by actual distance. That is a known, accepted residual at the authored
    // 6-10px surface heights, not a rule this pass enforces. Upright artwork and
    // actors then draw with depth against each other, all of them at or above
    // the ground plane.
    //
    // Splitting also keeps the batch whole: DepthMode is pipeline state, so
    // interleaving the two would flush between every alternating tile.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool uprightPass = (pass == 1);

        for (const size_t layerIdx : order)
        {
            const TileLayer& layer = m_Layers[layerIdx];

            for (int y = yScanStart; y <= y1; ++y)
            {
                const int rowOffset = y * m_MapWidth;
                const bool insideWindow = (y >= y0);

                for (int x = x0; x <= x1; ++x)
                {
                    const size_t idx = static_cast<size_t>(rowOffset + x);

                    int tileID = layer.tiles[idx];
                    if (tileID < 0)
                    {
                        continue;
                    }

                    const TileStance stance = layer.stance[idx];
                    const bool upright = tileRole::IsUpright(stance);
                    if (upright != uprightPass)
                    {
                        continue;
                    }
                    if (!upright && !insideWindow)
                    {
                        // Ground artwork in the northern margin is genuinely off screen.
                        continue;
                    }

                    if (idx < layer.animationMap.size())
                    {
                        const int animId = layer.animationMap[idx];
                        if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
                        {
                            tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
                        }
                    }
                    if (tileID < 0)
                    {
                        continue;
                    }

                    if (hasTransparencyCache && tileID < transparencyCacheSize &&
                        transparencyCache[tileID])
                    {
                        continue;
                    }

                    const float worldX = static_cast<float>(x) * tileWf;
                    const float worldY = static_cast<float>(y) * tileHf;

                    // Cheap reject against the real frustum before any quad is built.
                    // Centred half a tile above this cell's surface, so the sphere
                    // brackets upright artwork as well as flat ground and does not
                    // sit under a raised deck.
                    const float cullHeight =
                        elevationRole::SurfaceHeight(GetElevation(x, y), layer.elevationRole[idx]);
                    if (!frustum::IntersectsSphere(
                            viewFrustum,
                            sceneMath::ToScene({worldX + tileWf * 0.5f, worldY + tileHf * 0.5f},
                                               cullHeight + tileHf * 0.5f),
                            tileRadius))
                    {
                        continue;
                    }

                    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
                    if (upright)
                    {
                        // A Structure is authored as a vertical run of tiles: in the
                        // flat top-down view those rows paint one above the other and
                        // read as a single tall image. Standing each tile on its own
                        // grid row would put them at different DEPTHS - a column
                        // marching away from the camera instead of a wall - so the
                        // whole run is anchored on its bottom-most row and the tiles
                        // above it are lifted.
                        //
                        //   grid rows          scene
                        //   y   [roof ]         [roof ]   <- lifted 2 * tileH
                        //   y+1 [wall ]   =>    [wall ]   <- lifted 1 * tileH
                        //   y+2 [door ]         [door ]   <- stands on row y+2
                        //                      =========  ground
                        //
                        // A Prop or a Wall is the opposite reading of the same shape:
                        // a fence running north-south is three separate panels, each
                        // one tile tall on its own row, receding along the ground. It
                        // gets no base row and no lift, so it recedes instead of
                        // climbing into a tower. See tileRole::StacksVertically.
                        //
                        // The lift runs along the billboard's own up axis, not along
                        // world vertical. `up` leans north by the damped lean angle,
                        // so lifting vertically would leave each quad's top edge
                        // north of the next one's bottom edge and split the wall
                        // open along every tile seam.
                        //
                        // A Structure must also rotate as one rigid body. A narrow one
                        // still turns toward the camera, and quads turning about their
                        // own centers tear apart the moment the yaw leaves zero - a
                        // two-tile log breaks in half as the camera moves. Resolving
                        // one shared pivot for the whole body and placing each tile as
                        // a slice along the shared axes keeps it welded.
                        //
                        // Prop and Wall need none of that machinery. A Prop is a pole:
                        // it spins in place about its own center and there is nothing
                        // to weld it to. A Wall has yawFollow 0, so its right axis is
                        // exactly world +X and placing it at its own cell gives the
                        // same corners a shared pivot would - which means two Walls
                        // side by side derive their common edge from the identical
                        // grid expression and meet exactly, the same reason ground
                        // quads need no seam inflation.
                        const bool stacks = tileRole::StacksVertically(stance);

                        int runMinX = x;
                        int runMaxX = x;
                        int baseRow = y;

                        if (stacks)
                        {
                            // Prefer the authored no-projection structure: the editor
                            // already groups these tiles and the map caches each
                            // group's bounds, so the pivot is exact even for L-shaped
                            // or hollow buildings that a contiguity scan would
                            // mis-group. Tiles left on auto flood-fill
                            // (structureId < 0) fall back to scanning their run.
                            const int structId = layer.structureId[idx];
                            const StructureBounds* structBounds =
                                (structId >= 0) ? GetCachedStructureBounds(layerIdx, structId)
                                                : nullptr;

                            if (structBounds != nullptr)
                            {
                                runMinX = structBounds->minX;
                                runMaxX = structBounds->maxX;
                                baseRow = structBounds->maxY;
                            }
                            else
                            {
                                baseRow = FindStructureBaseRow(layer, x, y);
                                FindStructureRunColumns(layer, x, y, runMinX, runMaxX);
                            }
                        }

                        const float baseWorldY = static_cast<float>(baseRow) * tileHf;

                        // Pole or surface. For a Structure this is still measured -
                        // the extent of one authored body, so a narrow tower turns
                        // while a wide facade holds its footprint. For a Prop or a
                        // Wall the author decided, and neighbours never enter into it.
                        const int structureWidth = runMaxX - runMinX + 1;
                        const billboard::Orientation& orientation =
                            tileRole::IsGridLocked(stance, structureWidth) ? wallOrientation
                                                                           : pivotOrientation;

                        const float runCentreX =
                            static_cast<float>(runMinX + runMaxX + 1) * 0.5f * tileWf;
                        const float sliceOffset =
                            (static_cast<float>(x) + 0.5f) * tileWf - runCentreX;

                        // The whole body shares one foot height, taken at the run's
                        // center column on its base row, so a structure spanning
                        // cells of differing elevation stays rigid instead of
                        // tilting. For a one-wide Prop or Wall the center column is
                        // the tile's own column, so this is the same value either way.
                        const int footColumn = (runMinX + runMaxX) / 2;
                        const size_t footIdx =
                            static_cast<size_t>(baseRow) * static_cast<size_t>(m_MapWidth) +
                            static_cast<size_t>(footColumn);
                        const float footHeight = elevationRole::SurfaceHeight(
                            GetElevation(footColumn, baseRow), layer.elevationRole[footIdx]);

                        const glm::vec3 runFoot =
                            sceneMath::ToScene({runCentreX, baseWorldY + tileHf}, footHeight);

                        const float lift = static_cast<float>(baseRow - y) * tileHf;
                        billboard::MakeQuad(
                            runFoot + orientation.right * sliceOffset + orientation.up * lift,
                            tileSize,
                            orientation,
                            corners,
                            layer.rotation[idx]);
                    }
                    else
                    {
                        const SurfaceSlope slope = ResolveSurfaceSlope(layer, x, y);
                        sceneMath::MakeGroundQuad(
                            {worldX, worldY}, tileSize, 0.0f, layer.rotation[idx], corners);
                        sceneMath::ApplySlopeHeights({worldX, worldY},
                                                     tileSize,
                                                     slope.minus,
                                                     slope.plus,
                                                     slope.alongZ,
                                                     corners);
                    }

                    const int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
                    const int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

                    renderer.DrawQuad3D(
                        m_TilesetTexture,
                        corners,
                        glm::vec2(static_cast<float>(tilesetX), static_cast<float>(tilesetY)),
                        tileSize,
                        glm::vec4(1.0f),
                        renderModes::BlendMode::Alpha,
                        uprightPass ? renderModes::DepthMode::TestAndWrite
                                    : renderModes::DepthMode::None,
                        flipY,
                        layer.flipX[idx],
                        layer.flipY[idx]);
                }
            }
        }
    }
}

void Tilemap::RenderBackgroundLayers(IRenderer& renderer,
                                     glm::vec2 renderCam,
                                     glm::vec2 renderSize,
                                     glm::vec2 cullCam,
                                     glm::vec2 cullSize)
{
    // Single-pass rendering: iterate visible tiles once, render all background layers per tile
    auto order = GetLayerRenderOrder();

    // Collect background layer indices in render order
    std::vector<size_t> bgLayers;
    bgLayers.reserve(m_Layers.size());
    for (size_t idx : order)
    {
        if (m_Layers[idx].isBackground)
        {
            bgLayers.push_back(idx);
        }
    }
    if (bgLayers.empty())
        return;

    // Compute visible tile range once
    int x0, y0, x1, y1;
    ComputeTileRange(
        m_MapWidth, m_MapHeight, m_TileWidth, m_TileHeight, cullCam, cullSize, x0, y0, x1, y1);

    // Pre-compute constants
    const int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    const int mapWidth = m_MapWidth;
    const float tileWf = static_cast<float>(m_TileWidth);
    const float tileHf = static_cast<float>(m_TileHeight);
    const glm::vec2 texSize(tileWf, tileHf);
    const glm::vec2 tileRenderSize(tileWf, tileHf);
    const bool flipY = renderer.RequiresYFlip();
    const glm::vec3 white(1.0f);
    const bool hasTransparencyCache = m_TransparencyCacheBuilt;
    const std::vector<uint8_t>& transparencyCache = m_TileTransparencyCache;
    const int transparencyCacheSize = static_cast<int>(transparencyCache.size());

    // Single pass over visible tiles
    for (int y = y0; y <= y1; ++y)
    {
        const int rowOffset = y * mapWidth;
        const double tilePosYd =
            static_cast<double>(y) * m_TileHeight - static_cast<double>(renderCam.y);
        const float tilePosY = static_cast<float>(tilePosYd);

        for (int x = x0; x <= x1; ++x)
        {
            const size_t idx = static_cast<size_t>(rowOffset + x);
            const double tilePosXd =
                static_cast<double>(x) * m_TileWidth - static_cast<double>(renderCam.x);
            const float tilePosX = static_cast<float>(tilePosXd);

            // Render all background layers at this position (in render order)
            for (size_t layerIdx : bgLayers)
            {
                const TileLayer& layer = m_Layers[layerIdx];

                int tileID = layer.tiles[idx];

                if (tileID < 0)
                    continue;

                // Skip upright structures and Y-sorted tiles (rendered separately)
                if (layer.stance[idx] == TileStance::Structure || IsDepthSortedTile(x, y, layerIdx))
                    continue;

                // Apply animated tile frame if present
                if (idx < layer.animationMap.size())
                {
                    int animId = layer.animationMap[idx];
                    if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
                    {
                        tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
                    }
                }

                if (tileID < 0)
                    continue;

                // Skip transparent tiles
                if (hasTransparencyCache && tileID < transparencyCacheSize &&
                    transparencyCache[tileID])
                    continue;

                const int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
                const int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

                renderer.DrawSpriteRegion(
                    m_TilesetTexture,
                    glm::vec2(tilePosX, tilePosY),
                    tileRenderSize,
                    glm::vec2(static_cast<float>(tilesetX), static_cast<float>(tilesetY)),
                    texSize,
                    layer.rotation[idx],
                    white,
                    flipY,
                    layer.flipX[idx],
                    layer.flipY[idx]);
            }
        }
    }
}

// Foreground (post-player) counterpart of RenderBackgroundLayers: the same
// single-pass draw over the visible range, for the non-background layers.
void Tilemap::RenderForegroundLayers(IRenderer& renderer,
                                     glm::vec2 renderCam,
                                     glm::vec2 renderSize,
                                     glm::vec2 cullCam,
                                     glm::vec2 cullSize)
{
    // Single-pass rendering: iterate visible tiles once, render all foreground layers per tile
    auto order = GetLayerRenderOrder();

    // Collect foreground layer indices in render order
    std::vector<size_t> fgLayers;
    fgLayers.reserve(m_Layers.size());
    for (size_t idx : order)
    {
        if (!m_Layers[idx].isBackground)
        {
            fgLayers.push_back(idx);
        }
    }
    if (fgLayers.empty())
        return;

    // Compute visible tile range once
    int x0, y0, x1, y1;
    ComputeTileRange(
        m_MapWidth, m_MapHeight, m_TileWidth, m_TileHeight, cullCam, cullSize, x0, y0, x1, y1);

    // Pre-compute constants
    const int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    const int mapWidth = m_MapWidth;
    const float tileWf = static_cast<float>(m_TileWidth);
    const float tileHf = static_cast<float>(m_TileHeight);
    const glm::vec2 texSize(tileWf, tileHf);
    const glm::vec2 tileRenderSize(tileWf, tileHf);
    const bool flipY = renderer.RequiresYFlip();
    const glm::vec3 white(1.0f);
    const bool hasTransparencyCache = m_TransparencyCacheBuilt;
    const std::vector<uint8_t>& transparencyCache = m_TileTransparencyCache;
    const int transparencyCacheSize = static_cast<int>(transparencyCache.size());

    // Single pass over visible tiles
    for (int y = y0; y <= y1; ++y)
    {
        const int rowOffset = y * mapWidth;
        const double tilePosYd =
            static_cast<double>(y) * m_TileHeight - static_cast<double>(renderCam.y);
        const float tilePosY = static_cast<float>(tilePosYd);

        for (int x = x0; x <= x1; ++x)
        {
            const size_t idx = static_cast<size_t>(rowOffset + x);
            const double tilePosXd =
                static_cast<double>(x) * m_TileWidth - static_cast<double>(renderCam.x);
            const float tilePosX = static_cast<float>(tilePosXd);

            // Render all foreground layers at this position (in render order)
            for (size_t layerIdx : fgLayers)
            {
                const TileLayer& layer = m_Layers[layerIdx];

                int tileID = layer.tiles[idx];

                if (tileID < 0)
                    continue;

                // Skip upright structures and Y-sorted tiles (rendered separately)
                if (layer.stance[idx] == TileStance::Structure || IsDepthSortedTile(x, y, layerIdx))
                    continue;

                // Check for animated tile
                if (idx < layer.animationMap.size())
                {
                    int animId = layer.animationMap[idx];
                    if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
                    {
                        tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
                    }
                }

                if (tileID < 0)
                    continue;

                // Skip transparent tiles
                if (hasTransparencyCache && tileID < transparencyCacheSize &&
                    transparencyCache[tileID])
                    continue;

                const int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
                const int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

                renderer.DrawSpriteRegion(
                    m_TilesetTexture,
                    glm::vec2(tilePosX, tilePosY),
                    tileRenderSize,
                    glm::vec2(static_cast<float>(tilesetX), static_cast<float>(tilesetY)),
                    texSize,
                    layer.rotation[idx],
                    white,
                    flipY,
                    layer.flipX[idx],
                    layer.flipY[idx]);
            }
        }
    }
}

// Draw the no-projection tiles of the background / foreground layer sets. Thin
// wrappers selecting the layer set for the shared RenderLayersNoProjection body.
void Tilemap::RenderBackgroundLayersNoProjection(IRenderer& renderer,
                                                 glm::vec2 renderCam,
                                                 glm::vec2 renderSize,
                                                 glm::vec2 cullCam,
                                                 glm::vec2 cullSize)
{
    RenderLayersNoProjection(renderer, renderCam, renderSize, cullCam, cullSize, true);
}

void Tilemap::RenderForegroundLayersNoProjection(IRenderer& renderer,
                                                 glm::vec2 renderCam,
                                                 glm::vec2 renderSize,
                                                 glm::vec2 cullCam,
                                                 glm::vec2 cullSize)
{
    RenderLayersNoProjection(renderer, renderCam, renderSize, cullCam, cullSize, false);
}

// Draw the upright (TileStance::Structure) tiles of either the background or
// foreground layer set, in a single pass. Depth-sorted tiles are skipped here -
// they are emitted by the Y-sorted pass instead.
void Tilemap::RenderLayersNoProjection(IRenderer& renderer,
                                       glm::vec2 renderCam,
                                       glm::vec2 renderSize,
                                       glm::vec2 cullCam,
                                       glm::vec2 cullSize,
                                       bool isBackground)
{
    auto order = GetLayerRenderOrder();

    std::vector<size_t> layers;
    layers.reserve(m_Layers.size());
    for (size_t idx : order)
    {
        if (m_Layers[idx].isBackground == isBackground)
        {
            layers.push_back(idx);
        }
    }
    if (layers.empty())
        return;

    float cullPadX = STRUCTURE_SCAN_PADDING_TILES * static_cast<float>(m_TileWidth);
    float cullPadY = STRUCTURE_SCAN_PADDING_TILES * static_cast<float>(m_TileHeight);
    glm::vec2 expandedCullCam(cullCam.x - cullPadX, cullCam.y - cullPadY);
    glm::vec2 expandedCullSize(cullSize.x + cullPadX * 2.0f, cullSize.y + cullPadY * 2.0f);

    int x0, y0, x1, y1;
    ComputeTileRange(m_MapWidth,
                     m_MapHeight,
                     m_TileWidth,
                     m_TileHeight,
                     expandedCullCam,
                     expandedCullSize,
                     x0,
                     y0,
                     x1,
                     y1);

    const int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    const int mapWidth = m_MapWidth;
    const float tileWf = static_cast<float>(m_TileWidth);
    const float tileHf = static_cast<float>(m_TileHeight);
    const bool flipY = renderer.RequiresYFlip();
    const glm::vec3 white(1.0f);

    for (int y = y0; y <= y1; ++y)
    {
        const int rowOffset = y * mapWidth;
        const float tilePosY = y * tileHf - renderCam.y;

        for (int x = x0; x <= x1; ++x)
        {
            const size_t idx = static_cast<size_t>(rowOffset + x);
            const float tilePosX = x * tileWf - renderCam.x;

            for (size_t layerIdx : layers)
            {
                const TileLayer& layer = m_Layers[layerIdx];

                int tileID = layer.tiles[idx];

                if (tileID < 0)
                    continue;

                if (layer.stance[idx] != TileStance::Structure || IsDepthSortedTile(x, y, layerIdx))
                    continue;

                // Apply animated tile frame if present
                if (idx < layer.animationMap.size())
                {
                    int animId = layer.animationMap[idx];
                    if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
                    {
                        tileID = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
                    }
                }

                if (IsTileTransparent(tileID))
                    continue;

                int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
                int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

                renderer.DrawSpriteRegion(
                    m_TilesetTexture,
                    glm::vec2(tilePosX, tilePosY),
                    glm::vec2(tileWf, tileHf),
                    glm::vec2(static_cast<float>(tilesetX), static_cast<float>(tilesetY)),
                    glm::vec2(tileWf, tileHf),
                    layer.rotation[idx],
                    white,
                    flipY,
                    layer.flipX[idx],
                    layer.flipY[idx]);
            }
        }
    }
}

// Fill the ground layer with random non-transparent tiles sampled from the loaded
// tileset. Requires tileset data; logs and returns early if none is loaded or no
// usable (non-transparent) tiles are found.
void Tilemap::GenerateDefaultMap()
{
    // Validate tileset is loaded
    if (!m_TilesetData || m_TilesetDataWidth == 0 || m_TilesetDataHeight == 0)
    {
        Logger::Error(LOG_SUBSYSTEM, "Cannot generate map - tileset data not loaded!");
        return;
    }

    // Phases 1 and 2: scan the tileset for valid, non-transparent tiles.
    std::vector<int> validTileIDs;

    int totalTilesX = m_TilesetDataWidth / m_TileWidth;
    int totalTilesY = m_TilesetDataHeight / m_TileHeight;
    int totalTiles = totalTilesX * totalTilesY;

    Logger::Info(LOG_SUBSYSTEM, "Scanning tileset for non-transparent tiles...");
    Logger::InfoF(
        LOG_SUBSYSTEM, "  Tileset size: {}x{} pixels", m_TilesetDataWidth, m_TilesetDataHeight);
    Logger::InfoF(LOG_SUBSYSTEM, "  Tile size: {}x{} pixels", m_TileWidth, m_TileHeight);
    Logger::InfoF(LOG_SUBSYSTEM,
                  "  Total tiles in tileset: {}x{} = {} tiles",
                  totalTilesX,
                  totalTilesY,
                  totalTiles);

    for (int tileID = 0; tileID < totalTiles; ++tileID)
    {
        // Verify tile alignment (should always be true for sequential IDs)
        int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
        int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
        int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;

        if (tilesetX % m_TileWidth != 0 || tilesetY % m_TileHeight != 0)
        {
            continue;  // Skip misaligned tiles (shouldn't happen)
        }

        if (!IsTileTransparent(tileID))
        {
            validTileIDs.push_back(tileID);
        }
    }

    Logger::InfoF(LOG_SUBSYSTEM,
                  "Found {} non-transparent tiles out of {} total tiles",
                  validTileIDs.size(),
                  totalTiles);

    if (validTileIDs.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "No valid non-transparent tiles found in tileset!");
        return;
    }

    // Phase 3: fill the map with random valid tiles.
    std::mt19937 mapRng(std::random_device{}());
    std::uniform_int_distribution<int> tileDist(0, static_cast<int>(validTileIDs.size()) - 1);

    Logger::InfoF(LOG_SUBSYSTEM, "Generating random map with {} tiles...", MapCellCount());

    for (int y = 0; y < m_MapHeight; ++y)
    {
        for (int x = 0; x < m_MapWidth; ++x)
        {
            int randomIndex = tileDist(mapRng);
            int tileID = validTileIDs[randomIndex];
            SetLayerTile(x, y, 0, tileID);
        }
    }

    Logger::InfoF(LOG_SUBSYSTEM, "Generated random map with {} tiles", MapCellCount());
}

// Return the ids of every non-transparent tile in the loaded tileset, or an empty
// list if no tileset is loaded.
std::vector<int> Tilemap::GetValidTileIDs() const
{
    std::vector<int> validTileIDs;

    if (!m_TilesetData || m_TilesetDataWidth == 0 || m_TilesetDataHeight == 0)
    {
        return validTileIDs;
    }

    int totalTilesX = m_TilesetDataWidth / m_TileWidth;
    int totalTilesY = m_TilesetDataHeight / m_TileHeight;
    int totalTiles = totalTilesX * totalTilesY;

    for (int tileID = 0; tileID < totalTiles; ++tileID)
    {
        if (!IsTileTransparent(tileID))
        {
            validTileIDs.push_back(tileID);
        }
    }

    return validTileIDs;
}

// Parse a dialogue option's "when" string into a list of DialogueConditions.
// Grammar: terms joined by " & " (all must hold); each term is "flag" (set),
// "!flag" (not set), or "flag=value" (equals). Whitespace around a term is trimmed
// and empty terms are ignored.
static std::vector<DialogueCondition> ParseConditionString(const std::string& whenStr)
{
    std::vector<DialogueCondition> conditions;
    if (whenStr.empty())
        return conditions;

    // Split by " & " for AND conditions
    std::string remaining = whenStr;
    while (!remaining.empty())
    {
        size_t andPos = remaining.find(" & ");
        std::string part = (andPos != std::string::npos) ? remaining.substr(0, andPos) : remaining;
        remaining = (andPos != std::string::npos) ? remaining.substr(andPos + 3) : "";

        // Trim whitespace
        while (!part.empty() && part[0] == ' ')
            part.erase(0, 1);
        while (!part.empty() && part.back() == ' ')
            part.pop_back();
        if (part.empty())
            continue;

        DialogueCondition cond;

        // Check for negation
        bool negated = (part[0] == '!');
        if (negated)
            part.erase(0, 1);

        // Check for equals
        size_t eqPos = part.find('=');
        if (eqPos != std::string::npos)
        {
            cond.type = DialogueCondition::Type::FLAG_EQUALS;
            cond.key = part.substr(0, eqPos);
            cond.value = part.substr(eqPos + 1);
        }
        else
        {
            cond.type =
                negated ? DialogueCondition::Type::FLAG_NOT_SET : DialogueCondition::Type::FLAG_SET;
            cond.key = part;
        }

        conditions.push_back(cond);
    }
    return conditions;
}

// Parse a dialogue option's "do" array into a list of DialogueConsequences. Each
// string is "-flag" (clear), "flag=value" (set to value), "flag:desc" (set with a
// quest description), or "flag" (set). Non-string entries are ignored.
static std::vector<DialogueConsequence> ParseConsequenceArray(const nlohmann::json& doArr)
{
    std::vector<DialogueConsequence> consequences;
    if (!doArr.is_array())
        return consequences;

    for (const auto& item : doArr)
    {
        if (!item.is_string())
            continue;
        std::string str = item.get<std::string>();
        if (str.empty())
            continue;

        DialogueConsequence cons;

        // Check for clear flag prefix
        if (str[0] == '-')
        {
            cons.type = DialogueConsequence::Type::CLEAR_FLAG;
            cons.key = str.substr(1);
        }
        // Check for quest description (colon syntax for accepted_ flags)
        else if (str.find(':') != std::string::npos)
        {
            size_t colonPos = str.find(':');
            cons.type = DialogueConsequence::Type::SET_FLAG;
            cons.key = str.substr(0, colonPos);
            cons.value = str.substr(colonPos + 1);  // Quest description
        }
        // Check for value assignment
        else if (str.find('=') != std::string::npos)
        {
            size_t eqPos = str.find('=');
            cons.type = DialogueConsequence::Type::SET_FLAG_VALUE;
            cons.key = str.substr(0, eqPos);
            cons.value = str.substr(eqPos + 1);
        }
        // Simple flag set
        else
        {
            cons.type = DialogueConsequence::Type::SET_FLAG;
            cons.key = str;
        }

        consequences.push_back(cons);
    }
    return consequences;
}

// Inverse of ParseConditionString: join conditions back into a " & "-separated
// "when" string ("!flag", "flag=value", or "flag").
static std::string SerializeConditions(const std::vector<DialogueCondition>& conditions)
{
    if (conditions.empty())
        return "";

    std::string result;
    for (size_t i = 0; i < conditions.size(); ++i)
    {
        if (i > 0)
            result += " & ";
        const auto& c = conditions[i];

        if (c.type == DialogueCondition::Type::FLAG_NOT_SET)
            result += "!" + c.key;
        else if (c.type == DialogueCondition::Type::FLAG_EQUALS)
            result += c.key + "=" + c.value;
        else
            result += c.key;
    }
    return result;
}

// Inverse of ParseConsequenceArray: emit consequences as a JSON string array
// ("-flag", "flag=value", "flag:desc", or "flag").
static nlohmann::json SerializeConsequences(const std::vector<DialogueConsequence>& consequences)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : consequences)
    {
        if (c.type == DialogueConsequence::Type::CLEAR_FLAG)
            arr.push_back("-" + c.key);
        else if (c.type == DialogueConsequence::Type::SET_FLAG_VALUE)
            arr.push_back(c.key + "=" + c.value);
        else if (c.type == DialogueConsequence::Type::SET_FLAG && !c.value.empty())
            arr.push_back(c.key + ":" + c.value);  // Quest description
        else
            arr.push_back(c.key);
    }
    return arr;
}

// Serialize the whole map to a JSON file. Sparse by design: only non-default cells
// are written (per-layer tile/rotation/flag data), keeping files small. Also stores
// dimensions, collision/navigation index arrays, elevation, no-projection
// structures, particle zones, world lights, animated tiles, corner-cut masks, the
// NPC roster from the given registry (including dialogue trees), and the player
// spawn. Returns false if the file cannot be opened for writing.
bool Tilemap::SaveMapToJSON(const std::string& filename,
                            const ecs::registry* npcs,
                            int playerTileX,
                            int playerTileY,
                            int characterType) const
{
    using json = nlohmann::json;

    json j;

    // Map dimensions
    j["width"] = m_MapWidth;
    j["height"] = m_MapHeight;
    j["tileWidth"] = m_TileWidth;
    j["tileHeight"] = m_TileHeight;

    // Collision (array of indices)
    j["collision"] = m_CollisionMap.GetCollisionIndices();

    // Navigation (array of indices)
    j["navigation"] = m_NavigationMap.GetNavigationIndices();

    // Elevation (sparse object)
    {
        json elevObj = json::object();
        for (int y = 0; y < m_MapHeight; ++y)
        {
            for (int x = 0; x < m_MapWidth; ++x)
            {
                int elev = GetElevation(x, y);
                if (elev != 0)
                {
                    size_t index = FlatIndex(x, y);
                    elevObj[std::to_string(index)] = elev;
                }
            }
        }
        j["elevation"] = elevObj;
    }

    // Dynamic layers (all tile data stored here)
    json dynamicLayersArray = json::array();
    for (size_t layerIdx = 0; layerIdx < m_Layers.size(); ++layerIdx)
    {
        const TileLayer& layer = m_Layers[layerIdx];
        json layerJson = json::object();
        layerJson["name"] = layer.name;
        layerJson["renderOrder"] = layer.renderOrder;
        layerJson["isBackground"] = layer.isBackground;

        // Tiles (sparse)
        json tilesObj = json::object();
        for (size_t i = 0; i < layer.tiles.size(); ++i)
        {
            if (layer.tiles[i] != -1)
            {
                tilesObj[std::to_string(i)] = layer.tiles[i];
            }
        }
        layerJson["tiles"] = tilesObj;

        // Rotation (sparse)
        json rotObj = json::object();
        for (size_t i = 0; i < layer.rotation.size(); ++i)
        {
            if (layer.rotation[i] != 0.0f)
            {
                rotObj[std::to_string(i)] = layer.rotation[i];
            }
        }
        layerJson["rotation"] = rotObj;

        // Stance (sparse; an enum, so an object of values rather than an index
        // array - three parallel arrays could each claim the same cell, which is
        // precisely the ambiguity this field replaced). Omitted entirely when the
        // whole layer is Flat, like structureId. The legacy "noProjection" key is
        // never written again; LoadMapFromJSON migrates it.
        json stanceObj = json::object();
        for (size_t i = 0; i < layer.stance.size(); ++i)
        {
            if (layer.stance[i] != TileStance::Flat)
            {
                stanceObj[std::to_string(i)] =
                    static_cast<int>(std::to_underlying(layer.stance[i]));
            }
        }
        if (!stanceObj.empty())
        {
            layerJson["stance"] = stanceObj;
        }

        // Elevation role (sparse; same object-of-values shape as stance, and for
        // the same reason - parallel index arrays could each claim one cell).
        // Omitted when the whole layer is Ground, which is also exactly what a map
        // authored before this field looks like.
        json elevationRoleObj = json::object();
        for (size_t i = 0; i < layer.elevationRole.size(); ++i)
        {
            if (layer.elevationRole[i] != ElevationRole::Ground)
            {
                elevationRoleObj[std::to_string(i)] =
                    static_cast<int>(std::to_underlying(layer.elevationRole[i]));
            }
        }
        if (!elevationRoleObj.empty())
        {
            layerJson["elevationRole"] = elevationRoleObj;
        }

        // FlipX (array of indices for tiles mirrored around vertical axis)
        json flipXArr = json::array();
        for (size_t i = 0; i < layer.flipX.size(); ++i)
        {
            if (layer.flipX[i])
            {
                flipXArr.push_back(static_cast<int>(i));
            }
        }
        layerJson["flipX"] = flipXArr;

        // FlipY (array of indices for tiles mirrored around horizontal axis)
        json flipYArr = json::array();
        for (size_t i = 0; i < layer.flipY.size(); ++i)
        {
            if (layer.flipY[i])
            {
                flipYArr.push_back(static_cast<int>(i));
            }
        }
        layerJson["flipY"] = flipYArr;

        // YSortPlus (array of indices)
        json ySortPlusArr = json::array();
        for (size_t i = 0; i < layer.ySortPlus.size(); ++i)
        {
            if (layer.ySortPlus[i])
            {
                ySortPlusArr.push_back(static_cast<int>(i));
            }
        }
        layerJson["ySortPlus"] = ySortPlusArr;

        // YSortMinus (array of indices for Y-sorted tiles where player renders behind)
        json ySortMinusArr = json::array();
        for (size_t i = 0; i < layer.ySortMinus.size(); ++i)
        {
            if (layer.ySortMinus[i])
            {
                ySortMinusArr.push_back(static_cast<int>(i));
            }
        }
        layerJson["ySortMinus"] = ySortMinusArr;

        // StructureId (sparse - only save non-default values)
        json structIdObj = json::object();
        for (size_t i = 0; i < layer.structureId.size(); ++i)
        {
            if (layer.structureId[i] >= 0)
            {
                structIdObj[std::to_string(i)] = layer.structureId[i];
            }
        }
        if (!structIdObj.empty())
        {
            layerJson["structureId"] = structIdObj;
        }

        dynamicLayersArray.push_back(layerJson);
    }
    j["dynamicLayers"] = dynamicLayersArray;

    // No-Projection Structures (manually defined with anchors)
    if (!m_NoProjectionStructures.empty())
    {
        json structuresArray = json::array();
        for (const auto& s : m_NoProjectionStructures)
        {
            json structJson;
            structJson["id"] = s.id;
            if (!s.name.empty())
            {
                structJson["name"] = s.name;
            }
            structJson["leftAnchor"] = {s.leftAnchor.x, s.leftAnchor.y};
            structJson["rightAnchor"] = {s.rightAnchor.x, s.rightAnchor.y};
            structuresArray.push_back(structJson);
        }
        j["noProjectionStructures"] = structuresArray;
    }

    // Particle Zones
    json particleZonesArray = json::array();
    for (const auto& zone : m_ParticleZones)
    {
        json zoneJson;
        zoneJson["x"] = zone.position.x;
        zoneJson["y"] = zone.position.y;
        zoneJson["width"] = zone.size.x;
        zoneJson["height"] = zone.size.y;
        zoneJson["type"] = static_cast<int>(zone.type);
        zoneJson["enabled"] = zone.enabled;
        zoneJson["noProjection"] = zone.noProjection;
        particleZonesArray.push_back(zoneJson);
    }
    j["particleZones"] = particleZonesArray;

    // World Lights (sticky world day/night light pools)
    if (!m_Lights.empty())
    {
        json lightsArray = json::array();
        for (const auto& light : m_Lights)
        {
            json lightJson;
            lightJson["x"] = light.position.x;
            lightJson["y"] = light.position.y;
            lightJson["r"] = light.color.r;
            lightJson["g"] = light.color.g;
            lightJson["b"] = light.color.b;
            lightJson["radius"] = light.radius;
            lightJson["schedule"] =
                std::string(EnumTraits<LightSchedule>::ToString(light.schedule));
            lightsArray.push_back(lightJson);
        }
        j["worldLights"] = lightsArray;
    }

    // NPCs
    json npcsArray = json::array();
    if (npcs)
    {
        const WorldServices* svc = npcs->globals().find<WorldServices>();
        npcs->each<const Dialogue, const Patrol, const NpcTag>(
            [&](const Dialogue& dial, const Patrol& patrol)
            {
                json npcObj;
                npcObj["type"] = dial.type;
                npcObj["tileX"] = patrol.tileX;
                npcObj["tileY"] = patrol.tileY;
                if (!dial.name.empty())
                {
                    npcObj["name"] = dial.name;
                }
                if (!dial.text.empty())
                {
                    npcObj["dialogue"] = dial.text;
                }
                // Save dialogue tree (simplified format)
                if (svc != nullptr && svc->dialogue != nullptr && svc->dialogue->HasTree(dial.tree))
                {
                    const DialogueTree& tree = svc->dialogue->Get(dial.tree);
                    json treeJson;
                    if (tree.startNodeId != "start")
                        treeJson["start"] = tree.startNodeId;

                    // Default speaker: the first enumerated node's speaker (nodes is
                    // unordered, so this is arbitrary); lets matching nodes omit theirs
                    std::string defaultSpeaker = dial.name;
                    if (!tree.nodes.empty())
                        defaultSpeaker = tree.nodes.begin()->second.speaker;
                    if (!defaultSpeaker.empty())
                        treeJson["speaker"] = defaultSpeaker;

                    json nodesObj = json::object();
                    for (const auto& [nodeId, node] : tree.nodes)
                    {
                        json nodeJson;
                        if (node.speaker != defaultSpeaker)
                            nodeJson["speaker"] = node.speaker;
                        nodeJson["text"] = node.text;

                        json choicesArr = json::array();
                        for (const auto& opt : node.options)
                        {
                            json choiceJson;
                            choiceJson["text"] = opt.text;
                            if (!opt.nextNodeId.empty())
                                choiceJson["goto"] = opt.nextNodeId;
                            std::string whenStr = SerializeConditions(opt.conditions);
                            if (!whenStr.empty())
                                choiceJson["when"] = whenStr;
                            if (!opt.consequences.empty())
                                choiceJson["do"] = SerializeConsequences(opt.consequences);
                            choicesArr.push_back(choiceJson);
                        }
                        nodeJson["choices"] = choicesArr;
                        nodesObj[nodeId] = nodeJson;
                    }
                    treeJson["nodes"] = nodesObj;
                    npcObj["dialogueTree"] = treeJson;
                }
                npcsArray.push_back(npcObj);
                Logger::InfoF(LOG_SUBSYSTEM,
                              "  Saved NPC: {} at ({}, {})",
                              dial.type,
                              patrol.tileX,
                              patrol.tileY);
            });
        Logger::InfoF(LOG_SUBSYSTEM, "Saving {} NPCs to {}", npcsArray.size(), filename);
    }
    j["npcs"] = npcsArray;

    // Player position
    if (playerTileX >= 0 && playerTileY >= 0)
    {
        json playerObj;
        playerObj["tileX"] = playerTileX;
        playerObj["tileY"] = playerTileY;
        if (characterType >= 0)
        {
            playerObj["characterType"] = characterType;
        }
        j["player"] = playerObj;
    }
    else
    {
        j["player"] = nullptr;
    }

    // Animated Tiles - save animation definitions and placements
    json animatedTilesArray = json::array();
    for (const auto& anim : m_AnimatedTiles)
    {
        json animJson;
        animJson["frames"] = anim.frames;
        animJson["frameDuration"] = anim.frameDuration;
        animatedTilesArray.push_back(animJson);
    }
    j["animatedTiles"] = animatedTilesArray;

    // Animation Map - save per-layer animation maps (sparse format)
    json layerAnimMaps = json::array();
    for (size_t layerIdx = 0; layerIdx < m_Layers.size(); ++layerIdx)
    {
        json layerAnimObj = json::object();
        const auto& animMap = m_Layers[layerIdx].animationMap;
        for (size_t i = 0; i < animMap.size(); ++i)
        {
            if (animMap[i] >= 0)
            {
                layerAnimObj[std::to_string(i)] = animMap[i];
            }
        }
        layerAnimMaps.push_back(layerAnimObj);
    }
    j["layerAnimationMaps"] = layerAnimMaps;

    // Corner Cut Blocked - save as sparse array of indices with mask values
    {
        json cornerCutObj = json::object();
        for (size_t i = 0; i < m_CornerCutBlocked.size(); ++i)
        {
            if (m_CornerCutBlocked[i] != 0)
            {
                cornerCutObj[std::to_string(i)] = m_CornerCutBlocked[i];
            }
        }
        j["cornerCutBlocked"] = cornerCutObj;
    }

    // Write to file
    std::ofstream file(filename);
    if (!file.is_open())
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Could not open file for writing: {}", filename);
        return false;
    }

    file << j.dump(2);  // Pretty print with 2-space indent
    file.close();

    Logger::InfoF(LOG_SUBSYSTEM, "Map saved to {}", filename);
    return true;
}

// Load a map written by SaveMapToJSON, replacing all current map state. Tolerant of
// partial or legacy files: unknown / out-of-range entries are skipped with a capped
// warning count, and older key names (e.g. "ySorted", "navmesh", a flat
// "animationMap") are still accepted. SetTilemapSize resets state up front, so any
// mid-load exception is caught and the tilemap is reset to a clean empty state
// rather than left half-populated. Returns false on parse failure or such a reset.
bool Tilemap::LoadMapFromJSON(const std::string& filename,
                              ecs::registry* npcs,
                              int* playerTileX,
                              int* playerTileY,
                              int* characterType)
{
    using json = nlohmann::json;

    std::ifstream file(filename);
    if (!file.is_open())
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Could not open file for reading: {}", filename);
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Failed to parse JSON: {}", e.what());
        return false;
    }
    file.close();

    int width = j.value("width", 0);
    int height = j.value("height", 0);
    int tileWidth = j.value("tileWidth", 16);
    int tileHeight = j.value("tileHeight", 16);

    if (width <= 0 || height <= 0)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Invalid map dimensions in {}", filename);
        return false;
    }

    // Initialize tilemap. SetTilemapSize clears all prior layer/collision/nav
    // state, so from this point forward any mid-load exception must leave the
    // tilemap in a coherent (if empty) state rather than a half-populated one.
    // The big try/catch below enforces that invariant.
    m_TileWidth = tileWidth;
    m_TileHeight = tileHeight;
    SetTilemapSize(width, height, false);

    try
    {
        int loadWarningCount = 0;
        constexpr int kMaxLoadWarningsToPrint = 25;
        auto reportLoadWarning =
            [&](const std::string& section, const std::string& key, const std::string& message)
        {
            if (loadWarningCount < kMaxLoadWarningsToPrint)
            {
                Logger::WarnF(
                    LOG_SUBSYSTEM, "LoadMapFromJSON[{}] key '{}': {}", section, key, message);
            }
            ++loadWarningCount;
        };

        // Helper to load sparse tile layer {"index": value}
        auto loadTileLayer =
            [&](const std::string& name, std::function<void(int, int, int)> setTile)
        {
            if (!j.contains(name))
                return;
            const auto& layer = j[name];
            if (layer.is_object())
            {
                for (auto& [key, value] : layer.items())
                {
                    try
                    {
                        int index = std::stoi(key);
                        int tileID = value.get<int>();
                        int x = index % width;
                        int y = index / width;
                        if (x >= 0 && x < width && y >= 0 && y < height)
                            setTile(x, y, tileID);
                    }
                    catch (const std::exception& e)
                    {
                        reportLoadWarning(name, key, e.what());
                    }
                }
            }
        };

        // Helper to load sparse rotation layer
        auto loadRotationLayer =
            [&](const std::string& name, std::function<void(int, int, float)> setRot)
        {
            if (!j.contains(name))
                return;
            const auto& layer = j[name];
            if (layer.is_object())
            {
                for (auto& [key, value] : layer.items())
                {
                    try
                    {
                        int index = std::stoi(key);
                        float rot = value.get<float>();
                        int x = index % width;
                        int y = index / width;
                        if (x >= 0 && x < width && y >= 0 && y < height)
                            setRot(x, y, rot);
                    }
                    catch (const std::exception& e)
                    {
                        reportLoadWarning(name, key, e.what());
                    }
                }
            }
        };

        // Helper to load index array [idx1, idx2, ...]
        auto loadIndexArray =
            [&](const std::string& name, std::function<void(int, int, bool)> setFlag)
        {
            if (!j.contains(name))
                return;
            const auto& arr = j[name];
            if (arr.is_array())
            {
                for (const auto& idx : arr)
                {
                    try
                    {
                        int index = idx.get<int>();
                        int x = index % width;
                        int y = index / width;
                        if (x >= 0 && x < width && y >= 0 && y < height)
                            setFlag(x, y, true);
                    }
                    catch (const std::exception& e)
                    {
                        reportLoadWarning(name, "[array]", e.what());
                    }
                }
            }
        };

        // Load collision and navigation
        loadIndexArray("collision", [this](int x, int y, bool v) { SetTileCollision(x, y, v); });
        loadIndexArray("navigation", [this](int x, int y, bool v) { SetNavigation(x, y, v); });
        loadIndexArray("navmesh", [this](int x, int y, bool v) { SetNavigation(x, y, v); });

        // Load elevation
        loadTileLayer("elevation", [this](int x, int y, int v) { SetElevation(x, y, v); });

        // Load dynamic layers (new format)
        bool sizeMismatch = false;  // Track if layer data doesn't match new map size
        if (j.contains("dynamicLayers") && j["dynamicLayers"].is_array())
        {
            const auto& dynamicLayersArr = j["dynamicLayers"];
            m_Layers.clear();
            m_Layers.reserve(dynamicLayersArr.size());

            const size_t mapSize = static_cast<size_t>(width) * static_cast<size_t>(height);

            for (const auto& layerJson : dynamicLayersArr)
            {
                TileLayer layer;
                layer.name = layerJson.value("name", "");
                layer.renderOrder = layerJson.value("renderOrder", 0);
                layer.isBackground = layerJson.value("isBackground", true);
                layer.Resize(mapSize);

                // Load tiles (sparse object)
                if (layerJson.contains("tiles") && layerJson["tiles"].is_object())
                {
                    for (auto& [key, value] : layerJson["tiles"].items())
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(std::stoi(key));
                            if (index < mapSize)
                            {
                                layer.tiles[index] = value.get<int>();
                            }
                            else
                            {
                                sizeMismatch = true;  // Index out of bounds for new size
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.tiles", key, e.what());
                        }
                    }
                }

                // Load rotation (sparse object)
                if (layerJson.contains("rotation") && layerJson["rotation"].is_object())
                {
                    for (auto& [key, value] : layerJson["rotation"].items())
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(std::stoi(key));
                            if (index < mapSize)
                            {
                                layer.rotation[index] = value.get<float>();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.rotation", key, e.what());
                        }
                    }
                }

                // Legacy "noProjection" (array of indices). Only captured here -
                // it cannot be turned into a stance until the y-sort flags below
                // have been read, so the migration runs after them.
                std::vector<uint8_t> legacyNoProjection;
                if (layerJson.contains("noProjection") && layerJson["noProjection"].is_array())
                {
                    legacyNoProjection.assign(mapSize, 0);
                    for (const auto& idx : layerJson["noProjection"])
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(idx.get<int>());
                            if (index < mapSize)
                            {
                                legacyNoProjection[index] = 1;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.noProjection", "[array]", e.what());
                        }
                    }
                }

                // Load flipX (array of indices for tiles mirrored around vertical axis)
                if (layerJson.contains("flipX") && layerJson["flipX"].is_array())
                {
                    for (const auto& idx : layerJson["flipX"])
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(idx.get<int>());
                            if (index < mapSize)
                            {
                                layer.flipX[index] = true;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.flipX", "[array]", e.what());
                        }
                    }
                }

                // Load flipY (array of indices for tiles mirrored around horizontal axis)
                if (layerJson.contains("flipY") && layerJson["flipY"].is_array())
                {
                    for (const auto& idx : layerJson["flipY"])
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(idx.get<int>());
                            if (index < mapSize)
                            {
                                layer.flipY[index] = true;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.flipY", "[array]", e.what());
                        }
                    }
                }

                // Load ySortPlus (array of indices) - also supports legacy "ySorted" key
                const char* ySortPlusKey =
                    layerJson.contains("ySortPlus") ? "ySortPlus" : "ySorted";
                if (layerJson.contains(ySortPlusKey) && layerJson[ySortPlusKey].is_array())
                {
                    for (const auto& idx : layerJson[ySortPlusKey])
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(idx.get<int>());
                            if (index < mapSize)
                            {
                                layer.ySortPlus[index] = true;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.ySortPlus", "[array]", e.what());
                        }
                    }
                }

                // Load ySortMinus (array of indices)
                if (layerJson.contains("ySortMinus") && layerJson["ySortMinus"].is_array())
                {
                    for (const auto& idx : layerJson["ySortMinus"])
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(idx.get<int>());
                            if (index < mapSize)
                            {
                                layer.ySortMinus[index] = true;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.ySortMinus", "[array]", e.what());
                        }
                    }
                }

                // Load stance (sparse object of TileStance values), or migrate a
                // map authored before the field existed. A present "stance" key
                // wins outright, so a stale "noProjection" left over from an
                // older writer is ignored rather than fighting it.
                if (layerJson.contains("stance") && layerJson["stance"].is_object())
                {
                    for (auto& [key, value] : layerJson["stance"].items())
                    {
                        try
                        {
                            const size_t index = static_cast<size_t>(std::stoi(key));
                            const int raw = value.get<int>();
                            if (index < mapSize && raw >= 0 &&
                                static_cast<size_t>(raw) < TILE_STANCE_COUNT)
                            {
                                layer.stance[index] = static_cast<TileStance>(raw);
                            }
                            else if (index < mapSize)
                            {
                                reportLoadWarning(
                                    "dynamicLayers.stance", key, "value out of range");
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.stance", key, e.what());
                        }
                    }
                }
                else
                {
                    // m_Layers has not been grown yet, so its current size is this
                    // layer's own index.
                    MigrateLayerStance(layer, legacyNoProjection, m_Layers.size(), width, height);
                }

                // Elevation role (sparse object). Deliberately no migration: a map
                // without the key is all-Ground, which reproduces the pre-field
                // rendering exactly, so absence is already the right answer.
                if (layerJson.contains("elevationRole") && layerJson["elevationRole"].is_object())
                {
                    for (auto& [key, value] : layerJson["elevationRole"].items())
                    {
                        try
                        {
                            const size_t index = static_cast<size_t>(std::stoi(key));
                            const int raw = value.get<int>();
                            if (index < mapSize && raw >= 0 &&
                                static_cast<size_t>(raw) < ELEVATION_ROLE_COUNT)
                            {
                                layer.elevationRole[index] = static_cast<ElevationRole>(raw);
                            }
                            else if (index < mapSize)
                            {
                                reportLoadWarning(
                                    "dynamicLayers.elevationRole", key, "value out of range");
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.elevationRole", key, e.what());
                        }
                    }
                }

                // Load structureId (sparse object)
                if (layerJson.contains("structureId") && layerJson["structureId"].is_object())
                {
                    for (auto& [key, value] : layerJson["structureId"].items())
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(std::stoi(key));
                            if (index < mapSize)
                            {
                                layer.structureId[index] = value.get<int>();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.structureId", key, e.what());
                        }
                    }
                }

                m_Layers.push_back(std::move(layer));
            }
            Logger::InfoF(LOG_SUBSYSTEM, "Loaded {} dynamic layers", m_Layers.size());

            // If layer data doesn't match new map size, keep valid tiles and report truncation.
            if (sizeMismatch)
            {
                Logger::WarnF(LOG_SUBSYSTEM,
                              "Some dynamic layer entries were out of bounds for map size {}x{} "
                              "and were skipped.",
                              width,
                              height);
            }
        }

        // Load Particle Zones
        m_ParticleZones.clear();
        if (j.contains("particleZones") && j["particleZones"].is_array())
        {
            for (const auto& zoneJson : j["particleZones"])
            {
                ParticleZone zone;
                zone.position.x = zoneJson.value("x", 0.0f);
                zone.position.y = zoneJson.value("y", 0.0f);
                zone.size.x = zoneJson.value("width", 32.0f);
                zone.size.y = zoneJson.value("height", 32.0f);
                zone.type = static_cast<ParticleType>(zoneJson.value("type", 0));
                zone.enabled = zoneJson.value("enabled", true);
                zone.noProjection = zoneJson.value("noProjection", false);
                m_ParticleZones.push_back(zone);
            }
            Logger::InfoF(LOG_SUBSYSTEM, "Loaded {} particle zones", m_ParticleZones.size());
        }

        // Load World Lights (missing key = empty registry, backwards compatible)
        m_Lights.clear();
        if (j.contains("worldLights") && j["worldLights"].is_array())
        {
            for (const auto& lightJson : j["worldLights"])
            {
                WorldLight light;
                light.position.x = lightJson.value("x", 0.0f);
                light.position.y = lightJson.value("y", 0.0f);
                light.color.r = lightJson.value("r", 1.0f);
                light.color.g = lightJson.value("g", 0.85f);
                light.color.b = lightJson.value("b", 0.55f);
                light.radius = lightJson.value("radius", 64.0f);
                std::string scheduleName = lightJson.value("schedule", "NightOnly");
                auto sched = EnumTraits<LightSchedule>::FromString(scheduleName);
                light.schedule = sched.value_or(LightSchedule::NightOnly);
                m_Lights.push_back(light);
            }
            Logger::InfoF(LOG_SUBSYSTEM, "Loaded {} world lights", m_Lights.size());
        }

        // Load No-Projection Structures
        m_NoProjectionStructures.clear();
        if (j.contains("noProjectionStructures") && j["noProjectionStructures"].is_array())
        {
            for (const auto& structJson : j["noProjectionStructures"])
            {
                NoProjectionStructure s;
                s.id = structJson.value("id", static_cast<int>(m_NoProjectionStructures.size()));
                s.name = structJson.value("name", "");
                if (structJson.contains("leftAnchor") && structJson["leftAnchor"].is_array() &&
                    structJson["leftAnchor"].size() >= 2)
                {
                    s.leftAnchor.x = structJson["leftAnchor"][0].get<float>();
                    s.leftAnchor.y = structJson["leftAnchor"][1].get<float>();
                }
                if (structJson.contains("rightAnchor") && structJson["rightAnchor"].is_array() &&
                    structJson["rightAnchor"].size() >= 2)
                {
                    s.rightAnchor.x = structJson["rightAnchor"][0].get<float>();
                    s.rightAnchor.y = structJson["rightAnchor"][1].get<float>();
                }
                m_NoProjectionStructures.push_back(s);
            }
            Logger::InfoF(LOG_SUBSYSTEM,
                          "Loaded {} no-projection structures",
                          m_NoProjectionStructures.size());
        }

        // Load NPCs
        if (npcs && j.contains("npcs") && j["npcs"].is_array())
        {
            EntityStore::Clear(*npcs);
            for (const auto& npcJson : j["npcs"])
            {
                std::string type = npcJson.value("type", "");
                int tileX = npcJson.value("tileX", 0);
                int tileY = npcJson.value("tileY", 0);
                std::string name = npcJson.value("name", "");
                std::string dialogue = npcJson.value("dialogue", "");

                if (!type.empty())
                {
                    // Services for the spawn come from the registry's globals
                    // (WorldServices); SpawnNpc resolves the sheet + registers the
                    // tree and logs if the sprite fails to load.
                    NpcRecord record;
                    record.type = type;
                    record.name = name;
                    record.text = dialogue;
                    record.tileX = tileX;
                    record.tileY = tileY;
                    record.tileSize = tileWidth;

                    // Load dialogue tree (simplified format)
                    if (npcJson.contains("dialogueTree") && npcJson["dialogueTree"].is_object())
                    {
                        const auto& treeJson = npcJson["dialogueTree"];
                        DialogueTree tree;
                        tree.id = treeJson.value("id", type);
                        tree.startNodeId = treeJson.value("start", "start");
                        std::string defaultSpeaker = treeJson.value("speaker", name);

                        if (treeJson.contains("nodes") && treeJson["nodes"].is_object())
                        {
                            for (auto& [nodeId, nodeJson] : treeJson["nodes"].items())
                            {
                                DialogueNode node;
                                node.id = nodeId;
                                node.speaker = nodeJson.value("speaker", defaultSpeaker);
                                node.text = nodeJson.value("text", "");

                                if (nodeJson.contains("choices") && nodeJson["choices"].is_array())
                                {
                                    for (const auto& choiceJson : nodeJson["choices"])
                                    {
                                        DialogueOption opt;
                                        opt.text = choiceJson.value("text", "");
                                        opt.nextNodeId = choiceJson.value("goto", "");
                                        opt.conditions =
                                            ParseConditionString(choiceJson.value("when", ""));
                                        if (choiceJson.contains("do"))
                                            opt.consequences =
                                                ParseConsequenceArray(choiceJson["do"]);
                                        node.options.push_back(opt);
                                    }
                                }
                                tree.nodes[node.id] = node;
                            }
                        }
                        record.tree = std::move(tree);
                        record.hasTree = true;
                    }

                    EntityStore::SpawnNpc(*npcs, record);
                }
            }
            Logger::InfoF(LOG_SUBSYSTEM, "NPCs loaded: {}", EntityStore::Count(*npcs));
        }

        // Load player position
        if (j.contains("player") && !j["player"].is_null())
        {
            const auto& player = j["player"];
            if (playerTileX)
                *playerTileX = player.value("tileX", -1);
            if (playerTileY)
                *playerTileY = player.value("tileY", -1);
            if (characterType)
                *characterType = player.value("characterType", -1);
        }

        // Load animated tile definitions
        if (j.contains("animatedTiles") && j["animatedTiles"].is_array())
        {
            m_AnimatedTiles.clear();
            for (const auto& animJson : j["animatedTiles"])
            {
                AnimatedTile anim;
                if (animJson.contains("frames") && animJson["frames"].is_array())
                {
                    anim.frames = animJson["frames"].get<std::vector<int>>();
                }
                anim.frameDuration = animJson.value("frameDuration", 0.2f);
                m_AnimatedTiles.push_back(anim);
            }
            Logger::InfoF(
                LOG_SUBSYSTEM, "Loaded {} animated tile definitions", m_AnimatedTiles.size());
        }

        // Load per-layer animation maps (new format)
        size_t mapSize = MapCellCount();
        if (j.contains("layerAnimationMaps") && j["layerAnimationMaps"].is_array())
        {
            const auto& layerAnimMaps = j["layerAnimationMaps"];
            for (size_t layerIdx = 0; layerIdx < layerAnimMaps.size() && layerIdx < m_Layers.size();
                 ++layerIdx)
            {
                if (layerAnimMaps[layerIdx].is_object())
                {
                    auto& animMap = m_Layers[layerIdx].animationMap;
                    if (animMap.size() != mapSize)
                    {
                        animMap.assign(mapSize, -1);
                    }
                    for (auto& [key, value] : layerAnimMaps[layerIdx].items())
                    {
                        size_t idx;
                        try
                        {
                            idx = static_cast<size_t>(std::stoi(key));
                        }
                        catch (const std::invalid_argument& e)
                        {
                            Logger::WarnF(
                                LOG_SUBSYSTEM, "Invalid animation map key '{}': {}", key, e.what());
                            continue;
                        }
                        catch (const std::out_of_range& e)
                        {
                            Logger::WarnF(LOG_SUBSYSTEM,
                                          "Out-of-range animation map key '{}': {}",
                                          key,
                                          e.what());
                            continue;
                        }
                        if (idx < animMap.size())
                        {
                            animMap[idx] = value.get<int>();
                        }
                    }
                }
            }
            Logger::Info(LOG_SUBSYSTEM, "Loaded per-layer animation map placements");
        }
        // Backwards compatibility: load old "animationMap" format into layer 0
        else if (j.contains("animationMap") && j["animationMap"].is_object())
        {
            if (!m_Layers.empty())
            {
                auto& animMap = m_Layers[0].animationMap;
                if (animMap.size() != mapSize)
                {
                    animMap.assign(mapSize, -1);
                }
                for (auto& [key, value] : j["animationMap"].items())
                {
                    size_t idx;
                    try
                    {
                        idx = static_cast<size_t>(std::stoi(key));
                    }
                    catch (const std::invalid_argument& e)
                    {
                        Logger::WarnF(
                            LOG_SUBSYSTEM, "Invalid animation map key '{}': {}", key, e.what());
                        continue;
                    }
                    catch (const std::out_of_range& e)
                    {
                        Logger::WarnF(LOG_SUBSYSTEM,
                                      "Out-of-range animation map key '{}': {}",
                                      key,
                                      e.what());
                        continue;
                    }
                    if (idx < animMap.size())
                    {
                        animMap[idx] = value.get<int>();
                    }
                }
            }
            Logger::Info(LOG_SUBSYSTEM,
                         "Loaded animation map placements (legacy format -> layer 0)");
        }

        // Load Corner Cut Blocked data
        if (j.contains("cornerCutBlocked") && j["cornerCutBlocked"].is_object())
        {
            // Ensure vector is sized correctly
            if (m_CornerCutBlocked.size() != mapSize)
            {
                m_CornerCutBlocked.assign(mapSize, 0);
            }
            for (auto& [key, value] : j["cornerCutBlocked"].items())
            {
                size_t idx = static_cast<size_t>(std::stoi(key));
                if (idx < m_CornerCutBlocked.size())
                {
                    m_CornerCutBlocked[idx] = value.get<uint8_t>();
                }
            }
            Logger::Info(LOG_SUBSYSTEM, "Loaded corner cut blocked data");
        }

        // Debug: summarize animation state after load
        int animatedTileCount = 0;
        for (const auto& layer : m_Layers)
        {
            for (int a : layer.animationMap)
            {
                if (a >= 0)
                    animatedTileCount++;
            }
        }
        Logger::DebugF(
            LOG_SUBSYSTEM,
            "Animation state after load: {} definitions, {} placed tiles across all layers",
            m_AnimatedTiles.size(),
            animatedTileCount);
        for (size_t i = 0; i < m_AnimatedTiles.size(); ++i)
        {
            const auto& anim = m_AnimatedTiles[i];
            Logger::DebugF(LOG_SUBSYSTEM,
                           "  Animation #{}: {} frames, {}s/frame",
                           i,
                           anim.frames.size(),
                           anim.frameDuration);
        }

        if (loadWarningCount > 0)
        {
            Logger::WarnF(LOG_SUBSYSTEM,
                          "LoadMapFromJSON skipped {} malformed/out-of-range entries while "
                          "loading {}",
                          loadWarningCount,
                          filename);
        }

        InvalidateStructureBoundsCache();

        Logger::InfoF(LOG_SUBSYSTEM, "Map loaded from {} ({}x{})", filename, width, height);
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::ErrorF(
            LOG_SUBSYSTEM,
            "Mid-load exception in LoadMapFromJSON({}): {}. Resetting tilemap to a clean empty "
            "state.",
            filename,
            e.what());
        SetTilemapSize(width, height, false);
        InvalidateStructureBoundsCache();
        return false;
    }
    catch (...)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "Mid-load unknown exception in LoadMapFromJSON({}). Resetting tilemap to "
                       "a clean empty state.",
                       filename);
        SetTilemapSize(width, height, false);
        InvalidateStructureBoundsCache();
        return false;
    }
}
