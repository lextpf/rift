#include "Tilemap.h"
#include "NonPlayerCharacter.h"

#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>
#include <json.hpp>
#include <random>
#include <sstream>
#include <vector>

// Note: STB_IMAGE_IMPLEMENTATION is already defined in Texture.cpp
// We just need the header for function declarations
#include <stb_image.h>

#include "MathConstants.h"

namespace
{

// Extra cull padding prevents no-projection structures from popping at globe edges.
constexpr float NO_PROJECTION_CULL_PADDING_TILES = 8.0f;

/// Return true when every vertex of a warped quad is behind the sphere.
bool IsWarpedQuadFullyBehindSphere(IRenderer& renderer, const glm::vec2 (&corners)[4])
{
    int behindCount = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (renderer.IsPointBehindSphere(corners[i]))
            ++behindCount;
    }

    return behindCount == 4;
}

/// Return true when the full no-projection structure base segment is behind the globe.
/// We sample multiple points along the base to avoid false visibility on long bases.
bool IsStructureBaseFullyBehindSphere(IRenderer& renderer,
                                      float anchorMinScreenX,
                                      float anchorMaxScreenX,
                                      float bottomScreenY)
{
    constexpr int BASE_SAMPLES = 7;
    for (int i = 0; i < BASE_SAMPLES; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(BASE_SAMPLES - 1);
        float x = anchorMinScreenX + (anchorMaxScreenX - anchorMinScreenX) * t;
        if (!renderer.IsPointBehindSphere(glm::vec2(x, bottomScreenY)))
            return false;
    }
    return true;
}

/// Compute one edge point for a no-projection structure at an arbitrary world Y.
/// Accepts continuous Y so particles can stay locked to the same warped mesh.
glm::vec2 ComputeEdgePoint(IRenderer& renderer,
                           float anchorMinScreenX,
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
    return renderer.ComputeBuildingVertex(baseLeft, baseRight, u, v, heightWorld);
}

/// Compute horizon fade amount for no-projection structures in globe/fisheye mode.
/// 0 = fully visible, 1 = fully faded.
struct StructureHorizonFade
{
    float amount = 0.0f;
    bool columnFade = false;  // true: fade by columns, false: fade by rows
    bool fromLeft = true;
    bool fromTop = false;
};

StructureHorizonFade ComputeStructureHorizonFade(IRenderer& renderer,
                                                 const glm::vec2& structureBaseCenter,
                                                 float fadeBandPixels)
{
    StructureHorizonFade result;

    auto s = renderer.GetPerspectiveState();
    bool hasGlobe = s.enabled && (s.mode == IRenderer::ProjectionMode::Globe ||
                                  s.mode == IRenderer::ProjectionMode::Fisheye);
    if (!hasGlobe)
        return result;

    float centerX = s.viewWidth * 0.5f;
    float centerY = s.viewHeight * 0.5f;
    float dx = structureBaseCenter.x - centerX;
    float dy = structureBaseCenter.y - centerY;
    float baseR = s.sphereRadius;
    float radiusX = baseR * static_cast<float>(perspectiveTransform::kGlobeRadiusXScale);
    float radiusY = baseR * static_cast<float>(perspectiveTransform::kGlobeRadiusYScale);
    radiusX = std::max(1.0f, radiusX);
    radiusY = std::max(1.0f, radiusY);
    float dNorm = std::sqrt((dx * dx) / (radiusX * radiusX) + (dy * dy) / (radiusY * radiusY));
    float ndx = dx / radiusX;
    float ndy = dy / radiusY;

    float edgeNorm = rift::PiF * 0.5f;
    float avgRadius = (radiusX + radiusY) * 0.5f;
    float bandNorm = std::max(1e-4f, std::max(1.0f, fadeBandPixels) / std::max(1.0f, avgRadius));
    float start = edgeNorm - bandNorm;
    float end = edgeNorm + bandNorm;
    float t = (dNorm - start) / (end - start);
    result.amount = std::max(0.0f, std::min(1.0f, t));

    // Side-on globe fade should peel by columns; top/bottom fade should peel by rows.
    result.columnFade = std::abs(ndx) >= std::abs(ndy);
    result.fromLeft = ndx < 0.0f;
    result.fromTop = ndy < 0.0f;
    return result;
}

bool IsNoProjectionTileHiddenByHorizonFade(const StructureHorizonFade& fade,
                                           int tileCol,
                                           int structureWidthTiles,
                                           int tileY,
                                           int layerMinY,
                                           int effMaxY)
{
    if (fade.amount <= 0.0f)
        return false;

    if (fade.columnFade)
    {
        int hiddenCols = std::max(
            0,
            std::min(structureWidthTiles,
                     static_cast<int>(
                         std::floor(fade.amount * static_cast<float>(structureWidthTiles)))));
        if (hiddenCols <= 0)
            return false;

        if (fade.fromLeft)
            return tileCol < hiddenCols;

        return tileCol >= structureWidthTiles - hiddenCols;
    }

    int colHeightTiles = std::max(0, effMaxY - layerMinY + 1);
    if (colHeightTiles <= 0)
        return true;

    int hiddenRows = std::max(
        0,
        std::min(colHeightTiles,
                 static_cast<int>(std::floor(fade.amount * static_cast<float>(colHeightTiles)))));
    if (hiddenRows <= 0)
        return false;

    int rowFromTop = tileY - layerMinY;
    int rowFromBottom = effMaxY - tileY;
    if (fade.fromTop)
        return rowFromTop < hiddenRows;

    return rowFromBottom < hiddenRows;
}

bool IsNoProjectionPointHiddenByHorizonFade(const StructureHorizonFade& fade,
                                            float localXTiles,
                                            int structureWidthTiles,
                                            float worldTileY,
                                            int layerMinY,
                                            int effMaxY)
{
    if (fade.amount <= 0.0f)
        return false;

    if (fade.columnFade)
    {
        float hiddenCols = fade.amount * static_cast<float>(structureWidthTiles);
        if (hiddenCols <= 0.0f)
            return false;

        if (fade.fromLeft)
            return localXTiles < hiddenCols;

        return localXTiles >= static_cast<float>(structureWidthTiles) - hiddenCols;
    }

    float colHeightTiles = static_cast<float>(std::max(0, effMaxY - layerMinY + 1));
    if (colHeightTiles <= 0.0f)
        return true;

    float hiddenRows = fade.amount * colHeightTiles;
    if (hiddenRows <= 0.0f)
        return false;

    float rowFromTop = worldTileY - static_cast<float>(layerMinY);
    float rowFromBottom = static_cast<float>(effMaxY) - worldTileY;
    if (fade.fromTop)
        return rowFromTop < hiddenRows;

    return rowFromBottom < hiddenRows;
}

}  // namespace

Tilemap::Tilemap()
{
    // Allocate storage for all layers using row-major layout: size = width * height
    const size_t mapSize = MapCellCount();

    // Collision and navigation maps
    m_CollisionMap.Resize(m_MapWidth, m_MapHeight);
    m_NavigationMap.Resize(m_MapWidth, m_MapHeight);

    // Initialize 10 dynamic layers with proper render order:
    // Background layers (rendered before player/NPCs):
    //   Layer 0: Ground (renderOrder 0)
    //   Layer 1: Ground Detail (renderOrder 10)
    //   Layer 2: Objects (renderOrder 20)
    //   Layer 3: Objects2 (renderOrder 30)
    //   Layer 4: Objects3 (renderOrder 40)
    // Foreground layers (rendered after player/NPCs):
    //   Layer 5: Foreground (renderOrder 100)
    //   Layer 6: Foreground2 (renderOrder 110)
    //   Layer 7: Overlay (renderOrder 120)
    //   Layer 8: Overlay2 (renderOrder 130)
    //   Layer 9: Overlay3 (renderOrder 140)
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

void Tilemap::InvalidateStructureBoundsCache()
{
    m_StructureBoundsCacheDirty = true;
    m_DirtyStructureKeys.clear();
}

void Tilemap::InvalidateStructureBoundsForTile(
    size_t layerIdx, int x, int y, int oldStructId, int newStructId)
{
    if (m_StructureBoundsCacheDirty)
    {
        return;  // Full rebuild already pending
    }

    // Expand bounds for the new structure (O(1))
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
        // If the new structure was in the per-structure dirty set, clear it
        // since we just set bounds that include this tile.
        m_DirtyStructureKeys.erase(key);
    }

    // Mark old structure dirty for lazy re-scan (bounds may need to shrink)
    if (oldStructId >= 0 && oldStructId != newStructId)
    {
        int64_t key = (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(oldStructId);
        m_DirtyStructureKeys.insert(key);
    }
}

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

const Tilemap::StructureBounds* Tilemap::GetCachedStructureBounds(size_t layerIdx,
                                                                  int structId) const
{
    if (m_StructureBoundsCacheDirty)
    {
        RebuildStructureBoundsCache();
    }

    int64_t key = (static_cast<int64_t>(layerIdx) << 32) | static_cast<int64_t>(structId);

    // Re-scan this single structure if it was marked dirty
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
    std::cout << "Built transparency cache for " << totalTiles << " tiles" << std::endl;
}

bool Tilemap::LoadCombinedTilesets(const std::vector<std::string>& paths,
                                   int tileWidth,
                                   int tileHeight)
{
    if (paths.empty())
    {
        std::cerr << "ERROR: No tileset paths provided!" << std::endl;
        return false;
    }

    m_TileWidth = tileWidth;
    m_TileHeight = tileHeight;

    if (m_TileWidth <= 0 || m_TileHeight <= 0)
    {
        std::cerr << "ERROR: Invalid tile dimensions: " << m_TileWidth << "x" << m_TileHeight
                  << std::endl;
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
            std::cerr << "ERROR: Could not load tileset " << (i + 1) << ": " << paths[i]
                      << std::endl;
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
        std::cerr << "ERROR: No tilesets were loaded!" << std::endl;
        return false;
    }

    // Verify all tilesets have same channels
    int channels = tilesets[0].channels;
    for (size_t i = 1; i < tilesets.size(); ++i)
    {
        if (tilesets[i].channels != channels)
        {
            std::cerr << "ERROR: Tilesets must have the same number of channels! Tileset 1: "
                      << channels << ", Tileset " << (i + 1) << ": " << tilesets[i].channels
                      << std::endl;
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
        std::cerr << "ERROR: Failed to create combined texture!" << std::endl;
        for (auto& ts : tilesets)
        {
            stbi_image_free(ts.data);
        }
        return false;
    }

    // Store combined data for transparency checking (don't flip for data checking).
    // Transfer ownership from the local unique_ptr<unsigned char[]> to our TilesetDataPtr.
    m_TilesetData = TilesetDataPtr(combinedData.release(), +[](unsigned char* p) { delete[] p; });
    m_TilesetDataWidth = combinedWidth;
    m_TilesetDataHeight = combinedHeight;
    m_TilesetChannels = channels;

    m_TilesetWidth = combinedWidth;
    m_TilesetHeight = combinedHeight;
    m_TilesPerRow = m_TilesetWidth / m_TileWidth;

    std::cout << "Combined tileset dimensions: " << m_TilesetWidth << "x" << m_TilesetHeight
              << std::endl;
    for (size_t i = 0; i < tilesets.size(); ++i)
    {
        std::cout << "  Tileset " << (i + 1) << ": " << tilesets[i].width << "x"
                  << tilesets[i].height << " (" << (tilesets[i].width / m_TileWidth)
                  << " tiles wide) - " << paths[i] << std::endl;
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
            std::cout << "  Note: Tilesets have different widths. Narrower tilesets padded with "
                         "transparency."
                      << std::endl;
        }
    }
    std::cout << "Tile size: " << m_TileWidth << "x" << m_TileHeight << std::endl;
    std::cout << "Tiles per row: " << m_TilesPerRow << std::endl;
    std::cout << "Total tiles: "
              << (m_TilesetDataWidth / m_TileWidth) * (m_TilesetDataHeight / m_TileHeight)
              << std::endl;

    // Clean up temporary data (flippedData is auto-freed by unique_ptr)
    for (auto& ts : tilesets)
    {
        stbi_image_free(ts.data);
    }

    // Build transparency cache for all tiles
    BuildTransparencyCache();

    return true;
}

void Tilemap::SetTilemapSize(int width, int height, bool generateMap)
{
    m_MapWidth = width;
    m_MapHeight = height;

    const size_t mapSize = static_cast<size_t>(m_MapWidth) * static_cast<size_t>(m_MapHeight);

    m_Elevation.assign(mapSize, 0);

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

    m_Elevation[index] = elevation;
}

float Tilemap::GetElevationAtWorldPos(float worldX, float worldY) const
{
    // Convert world position to tile coordinates
    // Note: Entity positions use "feet position" convention where Y is at the
    // bottom of the tile (y * tileHeight + tileHeight). We subtract half a tile
    // height to correctly map feet position back to the occupied tile.
    int tileX = static_cast<int>(std::floor(worldX / m_TileWidth));
    int tileY = static_cast<int>(std::floor((worldY - m_TileHeight * 0.5f) / m_TileHeight));

    // Return elevation of current tile
    return static_cast<float>(GetElevation(tileX, tileY));
}

bool Tilemap::GetNoProjection(int x, int y, int layer) const
{
    if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
        return false;

    // Convert 1-indexed layer to 0-indexed dynamic layer
    size_t layerIdx = static_cast<size_t>(layer - 1);
    if (layerIdx >= m_Layers.size())
        return false;

    size_t index = FlatIndex(x, y);
    return m_Layers[layerIdx].noProjection[index];
}

bool Tilemap::FindNoProjectionStructureBounds(
    int tileX, int tileY, int& outMinX, int& outMaxX, int& outMinY, int& outMaxY) const
{
    if (tileX < 0 || tileX >= m_MapWidth || tileY < 0 || tileY >= m_MapHeight)
        return false;

    // Check if this tile has noProjection in ANY layer
    size_t idx = FlatIndex(tileX, tileY);
    bool hasNoProj = false;
    for (size_t li = 0; li < m_Layers.size(); ++li)
    {
        if (idx < m_Layers[li].noProjection.size() && m_Layers[li].noProjection[idx])
        {
            hasNoProj = true;
            break;
        }
    }
    if (!hasNoProj)
        return false;

    // Flood-fill to find all connected noProjection tiles (same as RenderLayerNoProjection).
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

        // Check if this tile is no-projection in ANY layer
        bool isNoProj = false;
        for (size_t li = 0; li < m_Layers.size(); ++li)
        {
            if (cIdx < m_Layers[li].noProjection.size() && m_Layers[li].noProjection[cIdx])
            {
                isNoProj = true;
                break;
            }
        }
        if (!isNoProj)
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

bool Tilemap::ProjectNoProjectionStructurePoint(IRenderer& renderer,
                                                const glm::vec2& worldPos,
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
            if (idx >= layer.noProjection.size() || !layer.noProjection[idx])
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

    if (IsStructureBaseFullyBehindSphere(
            renderer, anchorMinScreenX, anchorMaxScreenX, bottomScreenY))
        return false;

    float worldTileY = worldPos.y / tileHf;
    glm::vec2 structureBaseCenter((anchorMinScreenX + anchorMaxScreenX) * 0.5f, bottomScreenY);
    StructureHorizonFade horizonFade =
        ComputeStructureHorizonFade(renderer, structureBaseCenter, 64.0f);
    if (IsNoProjectionPointHiddenByHorizonFade(
            horizonFade, localXTiles, structureWidthTiles, worldTileY, minY, maxY))
        return false;

    glm::vec2 leftPoint = ComputeEdgePoint(renderer,
                                           anchorMinScreenX,
                                           anchorMaxScreenX,
                                           bottomScreenY,
                                           minY,
                                           maxY,
                                           m_TileHeight,
                                           structureWidthTiles,
                                           leftEdgeIndex,
                                           worldTileY);

    glm::vec2 rightPoint = ComputeEdgePoint(renderer,
                                            anchorMinScreenX,
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

const std::vector<Tilemap::YSortPlusTile>& Tilemap::GetVisibleYSortPlusTiles(
    glm::vec2 cullCam, glm::vec2 cullSize) const
{
    m_YSortPlusTilesCache.clear();

    float padX = NO_PROJECTION_CULL_PADDING_TILES * static_cast<float>(m_TileWidth);
    float padY = NO_PROJECTION_CULL_PADDING_TILES * static_cast<float>(m_TileHeight);
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

    // Helper to check if a tile at (x,y,layer) is Y-sorted and non-empty using dynamic layers
    auto isYSortPlusTile = [this](int x, int y, size_t layerIdx) -> bool
    {
        if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
            return false;
        if (layerIdx >= m_Layers.size())
            return false;
        size_t index = FlatIndex(x, y);
        const TileLayer& layer = m_Layers[layerIdx];
        if (index >= layer.ySortPlus.size())
            return false;
        if (!layer.ySortPlus[index])
            return false;
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

    // Check all dynamic layers for Y-sorted tiles
    for (size_t layerIdx = 0; layerIdx < m_Layers.size(); ++layerIdx)
    {
        const TileLayer& layer = m_Layers[layerIdx];

        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                size_t index = FlatIndex(x, y);
                if (index >= layer.ySortPlus.size())
                    continue;

                if (!layer.ySortPlus[index])
                    continue;

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

                // Find the bottom-most Y-sorted tile in this column (same x, same layer)
                // This groups vertically stacked tiles to sort together
                int bottomY = y;
                while (isYSortPlusTile(x, bottomY + 1, layerIdx))
                {
                    bottomY++;
                }

                YSortPlusTile tile;
                tile.x = x;
                tile.y = y;
                tile.layer = static_cast<int>(layerIdx);  // Store dynamic layer index
                // Use bottom tile's anchorY so entire vertical stack sorts together
                tile.anchorY = static_cast<float>((bottomY + 1) * m_TileHeight);
                // Check if this tile has no-projection flag
                tile.noProjection = layer.noProjection[index];
                // Use bottom tile's ySortMinus flag so entire vertical stack sorts consistently
                size_t bottomIndex = FlatIndex(x, bottomY);
                tile.ySortMinus = layer.ySortMinus[bottomIndex];
                m_YSortPlusTilesCache.push_back(tile);
            }
        }
    }

    return m_YSortPlusTilesCache;
}

void Tilemap::RenderSingleTile(
    IRenderer& renderer, int x, int y, int layer, glm::vec2 cameraPos, int useNoProjection)
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

    // Determine no-projection mode: -1=auto (from layer), 0=force off, 1=force on
    bool isNoProjection =
        (useNoProjection == -1) ? tileLayer.noProjection[index] : (useNoProjection == 1);

    int dataTilesPerRow = m_TilesetDataWidth / m_TileWidth;
    int tilesetX = (tileID % dataTilesPerRow) * m_TileWidth;
    int tilesetY = (tileID / dataTilesPerRow) * m_TileHeight;
    glm::vec2 texCoord(static_cast<float>(tilesetX), static_cast<float>(tilesetY));
    glm::vec2 texSize(static_cast<float>(m_TileWidth), static_cast<float>(m_TileHeight));
    bool flipY = renderer.RequiresYFlip();

    if (isNoProjection)
    {
        // Check if perspective is enabled
        bool perspectiveEnabled = renderer.GetPerspectiveState().enabled;

        if (!perspectiveEnabled)
        {
            // 2D mode: render directly like normal tiles
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
                                      flipY);
        }
        else
        {
            // 3D mode: use structure-based rendering if tile has structure ID
            int structId =
                (index < tileLayer.structureId.size()) ? tileLayer.structureId[index] : -1;

            if (structId >= 0 && structId < static_cast<int>(m_NoProjectionStructures.size()))
            {
                // Sphere-conforming warped building rendering
                // Each tile is rendered as a warped quad that bends to match the sphere curvature
                const NoProjectionStructure& structDef = m_NoProjectionStructures[structId];

                // Look up cached structure bounds (O(1) instead of full-map scan)
                const StructureBounds* bounds = GetCachedStructureBounds(layerIdx, structId);
                if (!bounds)
                    return;
                int minX = bounds->minX, maxX = bounds->maxX;
                int minY = bounds->minY, maxY = bounds->maxY;

                int structureWidthTiles = maxX - minX + 1;
                if (structureWidthTiles < 1)
                    return;

                int tileCol = x - minX;

                float anchorMinX = std::min(structDef.leftAnchor.x, structDef.rightAnchor.x);
                float anchorMaxX = std::max(structDef.leftAnchor.x, structDef.rightAnchor.x);
                float bottomWorldY = std::max(structDef.leftAnchor.y, structDef.rightAnchor.y);
                float bottomScreenY = bottomWorldY - cameraPos.y + 1.0f;

                float anchorMinScreenX = anchorMinX - cameraPos.x;
                float anchorMaxScreenX = anchorMaxX - cameraPos.x;

                if (IsStructureBaseFullyBehindSphere(
                        renderer, anchorMinScreenX, anchorMaxScreenX, bottomScreenY))
                    return;

                int leftEdgeIndex = tileCol;
                int rightEdgeIndex = tileCol + 1;
                if (leftEdgeIndex < 0 || rightEdgeIndex > structureWidthTiles)
                    return;

                float topTileY = static_cast<float>(y);
                float bottomTileY = topTileY + 1.0f;

                glm::vec2 structureBaseCenter((anchorMinScreenX + anchorMaxScreenX) * 0.5f,
                                              bottomScreenY);
                StructureHorizonFade horizonFade =
                    ComputeStructureHorizonFade(renderer, structureBaseCenter, 64.0f);
                if (IsNoProjectionTileHiddenByHorizonFade(
                        horizonFade, tileCol, structureWidthTiles, y, minY, maxY))
                    return;

                glm::vec2 corners[4];
                corners[0] = ComputeEdgePoint(renderer,
                                              anchorMinScreenX,
                                              anchorMaxScreenX,
                                              bottomScreenY,
                                              minY,
                                              maxY,
                                              m_TileHeight,
                                              structureWidthTiles,
                                              leftEdgeIndex,
                                              topTileY);
                corners[1] = ComputeEdgePoint(renderer,
                                              anchorMinScreenX,
                                              anchorMaxScreenX,
                                              bottomScreenY,
                                              minY,
                                              maxY,
                                              m_TileHeight,
                                              structureWidthTiles,
                                              rightEdgeIndex,
                                              topTileY);
                corners[2] = ComputeEdgePoint(renderer,
                                              anchorMinScreenX,
                                              anchorMaxScreenX,
                                              bottomScreenY,
                                              minY,
                                              maxY,
                                              m_TileHeight,
                                              structureWidthTiles,
                                              rightEdgeIndex,
                                              bottomTileY);
                corners[3] = ComputeEdgePoint(renderer,
                                              anchorMinScreenX,
                                              anchorMaxScreenX,
                                              bottomScreenY,
                                              minY,
                                              maxY,
                                              m_TileHeight,
                                              structureWidthTiles,
                                              leftEdgeIndex,
                                              bottomTileY);

                if (IsWarpedQuadFullyBehindSphere(renderer, corners))
                    return;

                // Render the tile as a warped quad (no additional perspective applied)
                renderer.DrawWarpedQuad(
                    m_TilesetTexture, corners, texCoord, texSize, glm::vec3(1.0f), flipY);
            }
            else
            {
                // No structure assigned - render with simple projection (legacy fallback)
                float worldX = static_cast<float>(x * m_TileWidth);
                float worldY = static_cast<float>(y * m_TileHeight);
                glm::vec2 screenPos(worldX - cameraPos.x, worldY - cameraPos.y);
                glm::vec2 renderSize(static_cast<float>(m_TileWidth),
                                     static_cast<float>(m_TileHeight));

                {
                    IRenderer::PerspectiveSuspendGuard guard(renderer);
                    renderer.DrawSpriteRegion(m_TilesetTexture,
                                              screenPos,
                                              renderSize,
                                              texCoord,
                                              texSize,
                                              rotation,
                                              glm::vec3(1.0f),
                                              flipY);
                }
            }
        }
    }
    else
    {
        // Normal rendering: let renderer handle perspective
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
                                  flipY);
    }
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

bool Tilemap::GetLayerNoProjection(int x, int y, size_t layer) const
{
    return GetLayerField<&TileLayer::noProjection>(x, y, layer);
}

void Tilemap::SetLayerNoProjection(int x, int y, size_t layer, bool noProjection)
{
    SetLayerField<&TileLayer::noProjection>(x, y, layer, noProjection);
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
    const float seamFix = renderer.GetPerspectiveState().enabled ? 0.1f : 0.0f;
    const glm::vec2 tileRenderSize(tileWf + seamFix, tileHf + seamFix);
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

            // Skip only when the whole tile quad is behind the sphere.
            glm::vec2 tileCorners[4] = {
                glm::vec2(tilePosX, tilePosY),
                glm::vec2(tilePosX + tileWf, tilePosY),
                glm::vec2(tilePosX + tileWf, tilePosY + tileHf),
                glm::vec2(tilePosX, tilePosY + tileHf),
            };
            if (IsWarpedQuadFullyBehindSphere(renderer, tileCorners))
                continue;

            // Render all background layers at this position (in render order)
            for (size_t layerIdx : bgLayers)
            {
                const TileLayer& layer = m_Layers[layerIdx];

                int tileID = layer.tiles[idx];

                if (tileID < 0)
                    continue;

                // Skip if no-projection or Y-sorted (rendered separately)
                if (layer.noProjection[idx] || layer.ySortPlus[idx])
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
                    flipY);
            }
        }
    }
}

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
    const float seamFix = renderer.GetPerspectiveState().enabled ? 0.1f : 0.0f;
    const glm::vec2 tileRenderSize(tileWf + seamFix, tileHf + seamFix);
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

            // Skip only when the whole tile quad is behind the sphere.
            glm::vec2 tileCorners[4] = {
                glm::vec2(tilePosX, tilePosY),
                glm::vec2(tilePosX + tileWf, tilePosY),
                glm::vec2(tilePosX + tileWf, tilePosY + tileHf),
                glm::vec2(tilePosX, tilePosY + tileHf),
            };
            if (IsWarpedQuadFullyBehindSphere(renderer, tileCorners))
                continue;

            // Render all foreground layers at this position (in render order)
            for (size_t layerIdx : fgLayers)
            {
                const TileLayer& layer = m_Layers[layerIdx];

                int tileID = layer.tiles[idx];

                if (tileID < 0)
                    continue;

                // Skip if no-projection or Y-sorted (rendered separately)
                if (layer.noProjection[idx] || layer.ySortPlus[idx])
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
                    flipY);
            }
        }
    }
}

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

    float cullPadX = NO_PROJECTION_CULL_PADDING_TILES * static_cast<float>(m_TileWidth);
    float cullPadY = NO_PROJECTION_CULL_PADDING_TILES * static_cast<float>(m_TileHeight);
    auto s = renderer.GetPerspectiveState();
    bool hasGlobe = s.enabled && (s.mode == IRenderer::ProjectionMode::Globe ||
                                  s.mode == IRenderer::ProjectionMode::Fisheye);
    if (hasGlobe)
    {
        // Globe projection compresses distant world-space tiles toward the edge,
        // so no-projection structures need a much wider world cull margin.
        float ovalPadXFactor =
            0.9f + 0.35f * static_cast<float>(perspectiveTransform::kGlobeRadiusXScale);
        float ovalPadYFactor =
            0.25f + 0.35f * static_cast<float>(perspectiveTransform::kGlobeRadiusYScale);
        cullPadX = std::max(cullPadX, s.viewWidth * ovalPadXFactor);
        cullPadY = std::max(cullPadY, s.viewHeight * ovalPadYFactor);
    }
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
    const bool perspectiveEnabled = renderer.GetPerspectiveState().enabled;

    if (!perspectiveEnabled)
    {
        // 2D mode: single-pass all matching layers
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

                    if (!layer.noProjection[idx] || layer.ySortPlus[idx])
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
                        flipY);
                }
            }
        }
        return;
    }

    // 3D mode: structure-based rendering with shared processed array
    size_t mapSize = MapCellCount();
    m_ProcessedCache.assign(mapSize, false);
    auto& processed = m_ProcessedCache;
    m_RenderedStructuresCache.assign(m_NoProjectionStructures.size(), false);
    auto& renderedStructures = m_RenderedStructuresCache;

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            size_t idx = FlatIndex(x, y);

            if (processed[idx])
                continue;

            bool hasNoProj = false;
            int foundStructId = -1;
            for (size_t layerIdx : layers)
            {
                if (m_Layers[layerIdx].noProjection[idx] && !m_Layers[layerIdx].ySortPlus[idx])
                {
                    hasNoProj = true;
                    if (idx < m_Layers[layerIdx].structureId.size() &&
                        m_Layers[layerIdx].structureId[idx] >= 0)
                    {
                        foundStructId = m_Layers[layerIdx].structureId[idx];
                    }
                    break;
                }
            }
            if (!hasNoProj)
                continue;

            if (foundStructId >= 0 &&
                foundStructId < static_cast<int>(m_NoProjectionStructures.size()))
            {
                if (renderedStructures[foundStructId])
                {
                    processed[idx] = true;
                    continue;
                }
                renderedStructures[foundStructId] = true;

                const NoProjectionStructure& structDef = m_NoProjectionStructures[foundStructId];

                struct LayerStructBounds
                {
                    bool valid = false;
                    int minX = 0, maxX = 0, minY = 0, maxY = 0;
                };
                std::vector<LayerStructBounds> layerBounds(m_Layers.size());
                std::vector<std::pair<int, int>> structureTiles;

                // Populate per-layer bounds from the structure bounds cache
                // instead of scanning the entire map.
                int unionMinX = m_MapWidth, unionMaxX = -1;
                int unionMinY = m_MapHeight, unionMaxY = -1;
                for (size_t layerIdx : layers)
                {
                    const auto* cached = GetCachedStructureBounds(layerIdx, foundStructId);
                    if (cached)
                    {
                        auto& b = layerBounds[layerIdx];
                        b.valid = true;
                        b.minX = cached->minX;
                        b.maxX = cached->maxX;
                        b.minY = cached->minY;
                        b.maxY = cached->maxY;
                        unionMinX = std::min(unionMinX, cached->minX);
                        unionMaxX = std::max(unionMaxX, cached->maxX);
                        unionMinY = std::min(unionMinY, cached->minY);
                        unionMaxY = std::max(unionMaxY, cached->maxY);
                    }
                }

                // Iterate only within (union bounds intersected with visible range)
                int scanMinX = std::max(unionMinX, x0);
                int scanMaxX = std::min(unionMaxX, x1);
                int scanMinY = std::max(unionMinY, y0);
                int scanMaxY = std::min(unionMaxY, y1);

                for (int sy = scanMinY; sy <= scanMaxY; ++sy)
                {
                    for (int sx = scanMinX; sx <= scanMaxX; ++sx)
                    {
                        size_t sIdx = FlatIndex(sx, sy);
                        if (processed[sIdx])
                            continue;

                        bool hasTileInStruct = false;
                        for (size_t layerIdx : layers)
                        {
                            if (!m_Layers[layerIdx].noProjection[sIdx] ||
                                m_Layers[layerIdx].ySortPlus[sIdx])
                                continue;
                            int sid = (sIdx < m_Layers[layerIdx].structureId.size())
                                          ? m_Layers[layerIdx].structureId[sIdx]
                                          : -1;
                            if (sid == foundStructId)
                            {
                                hasTileInStruct = true;
                                break;
                            }
                        }
                        if (!hasTileInStruct)
                            continue;

                        processed[sIdx] = true;
                        structureTiles.push_back({sx, sy});
                    }
                }

                if (structureTiles.empty())
                    continue;

                float anchorMinX = std::min(structDef.leftAnchor.x, structDef.rightAnchor.x);
                float anchorMaxX = std::max(structDef.leftAnchor.x, structDef.rightAnchor.x);
                float bottomWorldY = std::max(structDef.leftAnchor.y, structDef.rightAnchor.y);
                float bottomScreenY = bottomWorldY - renderCam.y + 1.0f;
                float anchorMinScreenX = anchorMinX - renderCam.x;
                float anchorMaxScreenX = anchorMaxX - renderCam.x;

                if (IsStructureBaseFullyBehindSphere(
                        renderer, anchorMinScreenX, anchorMaxScreenX, bottomScreenY))
                    continue;

                struct NoProjectionDrawCmd
                {
                    size_t layerIdx = 0;
                    int layerRank = 0;
                    int tx = 0;
                    int ty = 0;
                    int tid = -1;
                    int structureWidthTiles = 0;
                    int tileCol = 0;
                };
                std::vector<NoProjectionDrawCmd> drawCommands;
                drawCommands.reserve(structureTiles.size() * layers.size());

                for (const auto& [tx, ty] : structureTiles)
                {
                    size_t tIdx = FlatIndex(tx, ty);

                    for (size_t layerRank = 0; layerRank < layers.size(); ++layerRank)
                    {
                        size_t layerIdx = layers[layerRank];
                        const TileLayer& layer = m_Layers[layerIdx];

                        if (!layer.noProjection[tIdx] || layer.ySortPlus[tIdx])
                            continue;

                        int tid = layer.tiles[tIdx];
                        if (tid < 0)
                            continue;

                        if (tIdx < layer.animationMap.size())
                        {
                            int animId = layer.animationMap[tIdx];
                            if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()))
                            {
                                tid = m_AnimatedTiles[animId].GetFrameAtTime(m_AnimationTime);
                            }
                        }

                        if (IsTileTransparent(tid))
                            continue;

                        const auto& b = layerBounds[layerIdx];
                        if (!b.valid)
                            continue;

                        int structureWidthTiles = b.maxX - b.minX + 1;
                        if (structureWidthTiles < 1)
                            continue;

                        int tileCol = tx - b.minX;

                        NoProjectionDrawCmd cmd;
                        cmd.layerIdx = layerIdx;
                        cmd.layerRank = static_cast<int>(layerRank);
                        cmd.tx = tx;
                        cmd.ty = ty;
                        cmd.tid = tid;
                        cmd.structureWidthTiles = structureWidthTiles;
                        cmd.tileCol = tileCol;
                        drawCommands.push_back(cmd);
                    }
                }

                std::sort(drawCommands.begin(),
                          drawCommands.end(),
                          [](const auto& a, const auto& b)
                          {
                              if (a.layerRank != b.layerRank)
                                  return a.layerRank < b.layerRank;
                              if (a.ty != b.ty)
                                  return a.ty < b.ty;
                              return a.tx < b.tx;
                          });

                glm::vec2 structureBaseCenter((anchorMinScreenX + anchorMaxScreenX) * 0.5f,
                                              bottomScreenY);
                StructureHorizonFade horizonFade =
                    ComputeStructureHorizonFade(renderer, structureBaseCenter, 64.0f);

                for (const auto& cmd : drawCommands)
                {
                    const auto& b = layerBounds[cmd.layerIdx];
                    if (!b.valid)
                        continue;

                    if (IsNoProjectionTileHiddenByHorizonFade(horizonFade,
                                                              cmd.tileCol,
                                                              cmd.structureWidthTiles,
                                                              cmd.ty,
                                                              b.minY,
                                                              b.maxY))
                        continue;

                    int leftEdgeIndex = cmd.tileCol;
                    int rightEdgeIndex = cmd.tileCol + 1;

                    if (leftEdgeIndex < 0 || rightEdgeIndex > cmd.structureWidthTiles)
                        continue;

                    float topTileY = static_cast<float>(cmd.ty);
                    float bottomTileY = topTileY + 1.0f;

                    glm::vec2 corners[4];
                    corners[0] = ComputeEdgePoint(renderer,
                                                  anchorMinScreenX,
                                                  anchorMaxScreenX,
                                                  bottomScreenY,
                                                  b.minY,
                                                  b.maxY,
                                                  m_TileHeight,
                                                  cmd.structureWidthTiles,
                                                  leftEdgeIndex,
                                                  topTileY);
                    corners[1] = ComputeEdgePoint(renderer,
                                                  anchorMinScreenX,
                                                  anchorMaxScreenX,
                                                  bottomScreenY,
                                                  b.minY,
                                                  b.maxY,
                                                  m_TileHeight,
                                                  cmd.structureWidthTiles,
                                                  rightEdgeIndex,
                                                  topTileY);
                    corners[2] = ComputeEdgePoint(renderer,
                                                  anchorMinScreenX,
                                                  anchorMaxScreenX,
                                                  bottomScreenY,
                                                  b.minY,
                                                  b.maxY,
                                                  m_TileHeight,
                                                  cmd.structureWidthTiles,
                                                  rightEdgeIndex,
                                                  bottomTileY);
                    corners[3] = ComputeEdgePoint(renderer,
                                                  anchorMinScreenX,
                                                  anchorMaxScreenX,
                                                  bottomScreenY,
                                                  b.minY,
                                                  b.maxY,
                                                  m_TileHeight,
                                                  cmd.structureWidthTiles,
                                                  leftEdgeIndex,
                                                  bottomTileY);

                    if (IsWarpedQuadFullyBehindSphere(renderer, corners))
                        continue;

                    int tsX = (cmd.tid % dataTilesPerRow) * m_TileWidth;
                    int tsY = (cmd.tid / dataTilesPerRow) * m_TileHeight;

                    renderer.DrawWarpedQuad(
                        m_TilesetTexture,
                        corners,
                        glm::vec2(static_cast<float>(tsX), static_cast<float>(tsY)),
                        glm::vec2(tileWf, tileHf),
                        white,
                        flipY);
                }
            }
            else
            {
                // No defined structure - skip (requires structure assignment for no-projection)
                processed[idx] = true;
            }
        }
    }
}

void Tilemap::GenerateDefaultMap()
{
    // Validate tileset is loaded
    if (!m_TilesetData || m_TilesetDataWidth == 0 || m_TilesetDataHeight == 0)
    {
        std::cerr << "ERROR: Cannot generate map - tileset data not loaded!" << std::endl;
        return;
    }

    // === Phase 1 & 2: Scan tileset for valid (non-transparent) tiles ===
    std::vector<int> validTileIDs;

    int totalTilesX = m_TilesetDataWidth / m_TileWidth;
    int totalTilesY = m_TilesetDataHeight / m_TileHeight;
    int totalTiles = totalTilesX * totalTilesY;

    std::cout << "Scanning tileset for non-transparent tiles..." << std::endl;
    std::cout << "  Tileset size: " << m_TilesetDataWidth << "x" << m_TilesetDataHeight << " pixels"
              << std::endl;
    std::cout << "  Tile size: " << m_TileWidth << "x" << m_TileHeight << " pixels" << std::endl;
    std::cout << "  Total tiles in tileset: " << totalTilesX << "x" << totalTilesY << " = "
              << totalTiles << " tiles" << std::endl;

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

    std::cout << "Found " << validTileIDs.size() << " non-transparent tiles out of " << totalTiles
              << " total tiles" << std::endl;

    if (validTileIDs.empty())
    {
        std::cerr << "ERROR: No valid non-transparent tiles found in tileset!" << std::endl;
        return;
    }

    // === Phase 3: Fill map with random valid tiles ===
    std::mt19937 mapRng(std::random_device{}());
    std::uniform_int_distribution<int> tileDist(0, static_cast<int>(validTileIDs.size()) - 1);

    std::cout << "Generating random map with " << MapCellCount() << " tiles..." << std::endl;

    for (int y = 0; y < m_MapHeight; ++y)
    {
        for (int x = 0; x < m_MapWidth; ++x)
        {
            int randomIndex = tileDist(mapRng);
            int tileID = validTileIDs[randomIndex];
            SetLayerTile(x, y, 0, tileID);
        }
    }

    std::cout << "Generated random map with " << MapCellCount() << " tiles" << std::endl;
}

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

bool Tilemap::SaveMapToJSON(const std::string& filename,
                            const std::vector<NonPlayerCharacter>* npcs,
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

        // NoProjection (array of indices)
        json noProjArr = json::array();
        for (size_t i = 0; i < layer.noProjection.size(); ++i)
        {
            if (layer.noProjection[i])
            {
                noProjArr.push_back(static_cast<int>(i));
            }
        }
        layerJson["noProjection"] = noProjArr;

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

    // NPCs
    json npcsArray = json::array();
    if (npcs)
    {
        std::cout << "Saving " << npcs->size() << " NPCs to " << filename << std::endl;
        for (const auto& npc : *npcs)
        {
            json npcObj;
            npcObj["type"] = npc.GetType();
            npcObj["tileX"] = npc.GetTileX();
            npcObj["tileY"] = npc.GetTileY();
            if (!npc.GetName().empty())
            {
                npcObj["name"] = npc.GetName();
            }
            if (!npc.GetDialogue().empty())
            {
                npcObj["dialogue"] = npc.GetDialogue();
            }
            // Save dialogue tree (simplified format)
            if (npc.HasDialogueTree())
            {
                const DialogueTree& tree = npc.GetDialogueTree();
                json treeJson;
                if (tree.startNodeId != "start")
                    treeJson["start"] = tree.startNodeId;

                // Find default speaker (most common speaker in nodes)
                std::string defaultSpeaker = npc.GetName();
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
            std::cout << "  Saved NPC: " << npc.GetType() << " at (" << npc.GetTileX() << ", "
                      << npc.GetTileY() << ")" << std::endl;
        }
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
        std::cerr << "ERROR: Could not open file for writing: " << filename << std::endl;
        return false;
    }

    file << j.dump(2);  // Pretty print with 2-space indent
    file.close();

    std::cout << "Map saved to " << filename << std::endl;
    return true;
}

bool Tilemap::LoadMapFromJSON(const std::string& filename,
                              std::vector<NonPlayerCharacter>* npcs,
                              int* playerTileX,
                              int* playerTileY,
                              int* characterType)
{
    using json = nlohmann::json;

    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "ERROR: Could not open file for reading: " << filename << std::endl;
        return false;
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        std::cerr << "ERROR: Failed to parse JSON: " << e.what() << std::endl;
        return false;
    }
    file.close();

    int width = j.value("width", 0);
    int height = j.value("height", 0);
    int tileWidth = j.value("tileWidth", 16);
    int tileHeight = j.value("tileHeight", 16);

    if (width <= 0 || height <= 0)
    {
        std::cerr << "ERROR: Invalid map dimensions in " << filename << std::endl;
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
                std::cerr << "WARN: LoadMapFromJSON[" << section << "] key '" << key
                          << "': " << message << std::endl;
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

                // Load noProjection (array of indices)
                if (layerJson.contains("noProjection") && layerJson["noProjection"].is_array())
                {
                    for (const auto& idx : layerJson["noProjection"])
                    {
                        try
                        {
                            size_t index = static_cast<size_t>(idx.get<int>());
                            if (index < mapSize)
                            {
                                layer.noProjection[index] = true;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            reportLoadWarning("dynamicLayers.noProjection", "[array]", e.what());
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
            std::cout << "Loaded " << m_Layers.size() << " dynamic layers" << std::endl;

            // If layer data doesn't match new map size, keep valid tiles and report truncation.
            if (sizeMismatch)
            {
                std::cerr << "WARN: Some dynamic layer entries were out of bounds for map size "
                          << width << "x" << height << " and were skipped." << std::endl;
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
            std::cout << "Loaded " << m_ParticleZones.size() << " particle zones" << std::endl;
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
            std::cout << "Loaded " << m_NoProjectionStructures.size() << " no-projection structures"
                      << std::endl;
        }

        // Load NPCs
        if (npcs && j.contains("npcs") && j["npcs"].is_array())
        {
            npcs->clear();
            for (const auto& npcJson : j["npcs"])
            {
                std::string type = npcJson.value("type", "");
                int tileX = npcJson.value("tileX", 0);
                int tileY = npcJson.value("tileY", 0);
                std::string name = npcJson.value("name", "");
                std::string dialogue = npcJson.value("dialogue", "");

                if (!type.empty())
                {
                    NonPlayerCharacter npc;
                    if (npc.Load("assets/non-player/" + type + ".png"))
                    {
                        npc.SetTilePosition(tileX, tileY, tileWidth);
                        if (!name.empty())
                            npc.SetName(name);
                        if (!dialogue.empty())
                            npc.SetDialogue(dialogue);

                        // Load dialogue tree (simplified format)
                        if (npcJson.contains("dialogueTree") && npcJson["dialogueTree"].is_object())
                        {
                            const auto& treeJson = npcJson["dialogueTree"];
                            DialogueTree tree;
                            tree.id = treeJson.value("id", npc.GetType());
                            tree.startNodeId = treeJson.value("start", "start");
                            std::string defaultSpeaker = treeJson.value("speaker", npc.GetName());

                            if (treeJson.contains("nodes") && treeJson["nodes"].is_object())
                            {
                                for (auto& [nodeId, nodeJson] : treeJson["nodes"].items())
                                {
                                    DialogueNode node;
                                    node.id = nodeId;
                                    node.speaker = nodeJson.value("speaker", defaultSpeaker);
                                    node.text = nodeJson.value("text", "");

                                    if (nodeJson.contains("choices") &&
                                        nodeJson["choices"].is_array())
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
                            npc.SetDialogueTree(tree);
                        }

                        npcs->emplace_back(std::move(npc));
                    }
                }
            }
            std::cout << "NPCs loaded: " << npcs->size() << std::endl;
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
            std::cout << "Loaded " << m_AnimatedTiles.size() << " animated tile definitions"
                      << std::endl;
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
                            std::cerr << "WARNING: Invalid animation map key '" << key
                                      << "': " << e.what() << std::endl;
                            continue;
                        }
                        catch (const std::out_of_range& e)
                        {
                            std::cerr << "WARNING: Out-of-range animation map key '" << key
                                      << "': " << e.what() << std::endl;
                            continue;
                        }
                        if (idx < animMap.size())
                        {
                            animMap[idx] = value.get<int>();
                        }
                    }
                }
            }
            std::cout << "Loaded per-layer animation map placements" << std::endl;
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
                        std::cerr << "WARNING: Invalid animation map key '" << key
                                  << "': " << e.what() << std::endl;
                        continue;
                    }
                    catch (const std::out_of_range& e)
                    {
                        std::cerr << "WARNING: Out-of-range animation map key '" << key
                                  << "': " << e.what() << std::endl;
                        continue;
                    }
                    if (idx < animMap.size())
                    {
                        animMap[idx] = value.get<int>();
                    }
                }
            }
            std::cout << "Loaded animation map placements (legacy format -> layer 0)" << std::endl;
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
            std::cout << "Loaded corner cut blocked data" << std::endl;
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
        std::cout << "[DEBUG] Animation state after load: " << m_AnimatedTiles.size()
                  << " definitions, " << animatedTileCount << " placed tiles across all layers"
                  << std::endl;
        for (size_t i = 0; i < m_AnimatedTiles.size(); ++i)
        {
            const auto& anim = m_AnimatedTiles[i];
            std::cout << "  Animation #" << i << ": " << anim.frames.size() << " frames, "
                      << anim.frameDuration << "s/frame" << std::endl;
        }

        if (loadWarningCount > 0)
        {
            std::cerr << "WARN: LoadMapFromJSON skipped " << loadWarningCount
                      << " malformed/out-of-range entries while loading " << filename << std::endl;
        }

        InvalidateStructureBoundsCache();

        std::cout << "Map loaded from " << filename << " (" << width << "x" << height << ")"
                  << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: Mid-load exception in LoadMapFromJSON(" << filename
                  << "): " << e.what() << ". Resetting tilemap to a clean empty state."
                  << std::endl;
        SetTilemapSize(width, height, false);
        InvalidateStructureBoundsCache();
        return false;
    }
    catch (...)
    {
        std::cerr << "ERROR: Mid-load unknown exception in LoadMapFromJSON(" << filename
                  << "). Resetting tilemap to a clean empty state." << std::endl;
        SetTilemapSize(width, height, false);
        InvalidateStructureBoundsCache();
        return false;
    }
}
