#pragma once

#include "CollisionMap.h"
#include "ColumnProxy.h"
#include "IRenderer.h"
#include "NavigationMap.h"
#include "ParticleSystem.h"
#include "Texture.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declaration
class NonPlayerCharacter;

/**
 * @struct Tile
 * @brief Represents a single tile's position in the tileset texture.
 * @author Alex (https://github.com/lextpf)
 *
 * Used for tileset indexing and UV coordinate calculation.
 */
struct Tile
{
    int tileX;   ///< Column in tileset (0-based)
    int tileY;   ///< Row in tileset (0-based)
    int tileID;  ///< Unique identifier (tileY * tilesPerRow + tileX)
};

/**
 * @struct NoProjectionStructure
 * @brief Defines a no-projection structure with manually placed anchors.
 * @author Alex (https://github.com/lextpf)
 *
 * Structures are groups of tiles that bypass 3D projection. Instead of
 * automatic flood-fill detection, structures are manually defined with
 * explicit anchor positions for precise alignment control.
 */
struct NoProjectionStructure
{
    int id;                 ///< Unique structure ID (0+)
    std::string name;       ///< Optional name for editor display
    glm::vec2 leftAnchor;   ///< Left anchor world position (click corner of tile)
    glm::vec2 rightAnchor;  ///< Right anchor world position (click corner of tile)

    NoProjectionStructure()
        : id(-1),
          leftAnchor(-1.0f, -1.0f),
          rightAnchor(-1.0f, -1.0f)
    {
    }
    NoProjectionStructure(int structId, glm::vec2 left, glm::vec2 right, const std::string& n = "")
        : id(structId),
          name(n),
          leftAnchor(left),
          rightAnchor(right)
    {
    }
};

/**
 * @struct TileLayer
 * @brief Represents a single tile layer with all associated data.
 * @author Alex (https://github.com/lextpf)
 *
 * Each layer contains tile IDs, rotation values, and per-tile flags.
 * Layers are rendered in order based on their renderOrder value.
 */
struct TileLayer
{
    std::string name;                ///< Human-readable layer name
    std::vector<int> tiles;          ///< Tile IDs in row-major order (-1 = empty)
    std::vector<float> rotation;     ///< Rotation in degrees per tile
    std::vector<bool> noProjection;  ///< Tiles that bypass 3D projection
    std::vector<int>
        structureId;  ///< Per-tile structure ID (-1 = auto flood-fill, 0+ = belongs to structure)
    std::vector<bool> ySortPlus;  ///< Tiles that sort with entities by Y position (Y-sort+1: player
                                  ///< in front at same Y)
    std::vector<bool>
        ySortMinus;  ///< When true, player renders behind tile at same Y (Y-sort-1: tile in front)
    std::vector<int> animationMap;  ///< Per-tile animation ID (-1 = not animated)
    int renderOrder;    ///< Lower = rendered first (background), higher = later (foreground)
    bool isBackground;  ///< true = before player/NPCs, false = after

    TileLayer()
        : renderOrder(0),
          isBackground(true)
    {
    }
    TileLayer(const std::string& n, int order, bool bg)
        : name(n),
          renderOrder(order),
          isBackground(bg)
    {
    }

    /// @brief Resize all per-tile vectors to the given number of tiles.
    /// @param size Total number of tiles (mapWidth * mapHeight).
    void Resize(size_t size)
    {
        tiles.resize(size, -1);
        rotation.resize(size, 0.0f);
        noProjection.resize(size, false);
        structureId.resize(size, -1);
        ySortPlus.resize(size, false);
        ySortMinus.resize(size, false);
        animationMap.resize(size, -1);
    }

    /// @brief Reset all per-tile data to default values without changing size.
    void Clear()
    {
        std::fill(tiles.begin(), tiles.end(), -1);
        std::fill(rotation.begin(), rotation.end(), 0.0f);
        std::fill(noProjection.begin(), noProjection.end(), false);
        std::fill(structureId.begin(), structureId.end(), -1);
        std::fill(ySortPlus.begin(), ySortPlus.end(), false);
        std::fill(ySortMinus.begin(), ySortMinus.end(), false);
        std::fill(animationMap.begin(), animationMap.end(), -1);
    }
};

/**
 * @struct AnimatedTile
 * @brief Definition of an animated tile sequence.
 * @author Alex (https://github.com/lextpf)
 */
struct AnimatedTile
{
    std::vector<int> frames;  ///< Tile IDs for each frame
    float frameDuration;      ///< Seconds per frame

    AnimatedTile()
        : frameDuration(0.2f)
    {
    }
    AnimatedTile(const std::vector<int>& f, float duration = 0.2f)
        : frames(f),
          frameDuration(duration)
    {
    }

    /**
     * @brief Get the tile ID for a given elapsed time.
     * @param time Elapsed animation time in seconds.
     * @return Tile ID for the current frame, or -1 if no frames exist.
     */
    int GetFrameAtTime(float time) const
    {
        if (frames.empty())
            return -1;
        if (frameDuration <= 0.0f)
            return frames[0];  // Prevent division by zero
        int frameIndex = static_cast<int>(time / frameDuration) % static_cast<int>(frames.size());
        return frames[frameIndex];
    }
};

/**
 * @class Tilemap
 * @brief Multi-layer tile-based world with collision and navigation.
 * @author Alex (https://github.com/lextpf)
 *
 * The Tilemap class is the primary world representation, managing:
 * - **10 tile layers** (5 background, 5 foreground) with configurable depth ordering
 * - **Collision detection** for player movement
 * - **Navigation mesh** for NPC pathfinding
 * - **Per-tile rotation** for visual variety
 * - **JSON serialization** using a `dynamicLayers` format
 *
 * @par Layer Architecture
 * The tilemap uses 10 dynamic layers rendered around player/NPC entities:
 *
 * | Layer | Name            | Render Order | Purpose                    |
 * |-------|-----------------|--------------|----------------------------|
 * | 0     | Ground          | 0            | Base terrain               |
 * | 1     | Ground Detail   | 10           | Grass, paths, decorations  |
 * | 2     | Objects         | 20           | Buildings, rocks, trees    |
 * | 3     | Objects2        | 30           | Additional objects         |
 * | 4     | Objects3        | 40           | Additional objects         |
 * | 5     | Foreground      | 100          | Elements in front of NPCs  |
 * | 6     | Foreground2     | 110          | Additional foreground      |
 * | 7     | Overlay         | 120          | Overlay effects            |
 * | 8     | Overlay2        | 130          | Additional overlay         |
 * | 9     | Overlay3        | 140          | Top-most overlay           |
 *
 * @par Depth Sorting Visualization
 * @code
 *              Layer 9 Overlay3     <- Top (front)
 *              Layer 8 Overlay2
 *              Layer 7 Overlay
 *              Layer 6 Foreground2
 *              Layer 5 Foreground
 *              ---- Player -------
 *              ---- NPCs ---------
 *              Layer 4 Objects3
 *              Layer 3 Objects2
 *              Layer 2 Objects
 *              Layer 1 Ground Detail
 *              Layer 0 Ground       <- Bottom (back)
 * @endcode
 *
 * This ordering allows characters to walk behind foreground layers (5-9)
 * while appearing in front of background layers (0-4).
 *
 * @par Tile ID System
 * Tile IDs map directly to tileset positions:
 * @f[
 * tileID = tileY \times tilesPerRow + tileX
 * @f]
 *
 * Where (tileX, tileY) are the tile's coordinates in the tileset texture.
 * A tileID of -1 represents an empty/transparent tile.
 *
 * @par UV Coordinate Calculation
 * For a tile at tileset position (tx, ty):
 * @f[
 * u_0 = \frac{tx \times tileWidth}{textureWidth}, \quad
 * v_0 = \frac{ty \times tileHeight}{textureHeight}
 * @f]
 * @f[
 * u_1 = \frac{(tx + 1) \times tileWidth}{textureWidth}, \quad
 * v_1 = \frac{(ty + 1) \times tileHeight}{textureHeight}
 * @f]
 *
 * @par Coordinate System
 * The tilemap uses a top-left origin with Y increasing downward:
 * @code
 *   (0,0)-----> +X
 *     |
 *     |  Tile (x,y) at world position (x*16, y*16)
 *     v
 *    +Y
 * @endcode
 *
 * @par World-to-Tile Conversion
 * @f[
 * tile_x = \lfloor \frac{world_x}{tileWidth} \rfloor, \quad
 * tile_y = \lfloor \frac{world_y}{tileHeight} \rfloor
 * @f]
 *
 * @par Tile-to-World Conversion
 * @f[
 * world_x = tile_x \times tileWidth, \quad
 * world_y = tile_y \times tileHeight
 * @f]
 *
 * @par Memory Layout
 * Each layer stores tiles in row-major order:
 * @f[
 * index = y \times mapWidth + x
 * @f]
 *
 * @par Tileset Combination
 * Multiple tileset images can be combined vertically into a single texture:
 * @code
 *    +------------------+
 *    |    Tileset 1     |
 *    |    (256x256)     |
 *    +------------------+
 *    |    Tileset 2     |  <- Combined texture
 *    |    (256x128)     |
 *    +------------------+
 *    |    Tileset 3     |
 *    |    (256x64)      |
 *    +------------------+
 * @endcode
 *
 * @par Sparse Storage Format
 * When serialized to JSON, only non-empty tiles (tileID != -1) are stored
 * within each entry of the `dynamicLayers` array:
 * @code{.json}
 * {
 *   "dynamicLayers": [
 *     {
 *       "name": "Ground",
 *       "tiles": {
 *         "42": 15,    // Tile at index 42 = tile ID 15
 *         "100": 23    // Tile at index 100 = tile ID 23
 *       }
 *     }
 *   ]
 * }
 * @endcode
 *
 * This significantly reduces file size for large, sparse maps.
 *
 * @see CollisionMap, NavigationMap, ColumnProxy
 */
class Tilemap
{
public:
    /**
     * @brief Construct an empty Tilemap.
     *
     * Call LoadCombinedTilesets() and SetTilemapSize() before use.
     */
    Tilemap();

    /**
     * @brief Destructor releases tileset texture resources.
     */
    ~Tilemap();

    /**
     * @brief Load and combine multiple tileset images vertically.
     *
     * @param paths Vector of tileset paths (combined top-to-bottom).
     * @param tileWidth Tile width in pixels.
     * @param tileHeight Tile height in pixels.
     * @return `true` if all loaded and combined successfully.
     */
    bool LoadCombinedTilesets(const std::vector<std::string>& paths,
                              int tileWidth = 16,
                              int tileHeight = 16);

    /**
     * @brief Set the tilemap dimensions.
     *
     * Allocates storage for all layers, collision, and navigation.
     * Optionally generates a default map pattern.
     *
     * @par World Size
     * World dimensions in pixels:
     * @f[
     * worldWidth = width \times tileWidth
     * @f]
     * @f[
     * worldHeight = height \times tileHeight
     * @f]
     *
     * @param width Map width in tiles.
     * @param height Map height in tiles.
     * @param generateMap If true, fills with a default pattern.
     */
    void SetTilemapSize(int width, int height, bool generateMap = true);

    /**
     * @name Corner Cutting Control
     * @brief Per-tile flags to disable corner cutting on specific corners.
     *
     * Corner cutting allows diagonal movement past collision tile corners.
     * These flags let you block corner cutting on specific corners.
     * Each corner is identified by its position: TL=0, TR=1, BL=2, BR=3.
     * @{
     */

    /// Corner identifiers for corner cutting control
    enum Corner : uint8_t
    {
        CORNER_TL = 0,  ///< Top-left corner
        CORNER_TR = 1,  ///< Top-right corner
        CORNER_BL = 2,  ///< Bottom-left corner
        CORNER_BR = 3   ///< Bottom-right corner
    };

    /**
     * @brief Set whether corner cutting is blocked at a specific corner of a tile.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param corner Which corner (CORNER_TL, CORNER_TR, CORNER_BL, CORNER_BR).
     * @param blocked true to block corner cutting at this corner.
     */
    void SetCornerCutBlocked(int x, int y, Corner corner, bool blocked);

    /**
     * @brief Check if corner cutting is blocked at a specific corner.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param corner Which corner to check.
     * @return true if corner cutting is blocked at this corner.
     */
    bool IsCornerCutBlocked(int x, int y, Corner corner) const;
    /** @} */

    /**
     * @name Collision Functions
     * @brief Per-tile collision flags for player movement.
     *
     * Collision only applies to the Ground layer (index 0). Other layers
     * are purely decorative and don't affect movement.
     * @{
     */
    /**
     * @brief Set collision flag for a tile.
     *
     * Blocking tiles prevent the player from moving onto them.
     *
     * @param x Tile column.
     * @param y Tile row.
     * @param hasCollision `true` to block movement.
     */
    void SetTileCollision(int x, int y, bool hasCollision);

    /**
     * @brief Query collision flag for a tile.
     *
     * @param x Tile column.
     * @param y Tile row.
     * @return `true` if tile blocks movement.
     */
    bool GetTileCollision(int x, int y) const;

    /**
     * @brief Get mutable reference to the collision map.
     *
     * For advanced operations like bulk updates or direct indexing.
     *
     * @return Reference to CollisionMap.
     */
    CollisionMap<std::vector>& GetCollisionMap() { return m_CollisionMap; }

    /**
     * @brief Get read-only reference to the collision map.
     * @return Const reference to CollisionMap.
     */
    const CollisionMap<std::vector>& GetCollisionMap() const { return m_CollisionMap; }
    /** @} */

    /**
     * @name Navigation Functions
     * @brief NPC walkability flags for pathfinding.
     *
     * Navigation is independent of collision. NPCs only walk on
     * tiles marked as walkable, regardless of collision state.
     * @{
     */
    /**
     * @brief Set navigation flag for a tile.
     *
     * Walkable tiles can be included in NPC patrol routes.
     *
     * @param x Tile column.
     * @param y Tile row.
     * @param walkable `true` if NPCs can walk here.
     */
    void SetNavigation(int x, int y, bool walkable);

    /**
     * @brief Query navigation flag for a tile.
     *
     * @param x Tile column.
     * @param y Tile row.
     * @return `true` if NPCs can walk here.
     */
    bool GetNavigation(int x, int y) const;

    /**
     * @brief Get mutable reference to the navigation map.
     * @return Reference to NavigationMap.
     */
    NavigationMap<std::vector>& GetNavigationMap() { return m_NavigationMap; }

    /**
     * @brief Get read-only reference to the navigation map.
     * @return Const reference to NavigationMap.
     */
    const NavigationMap<std::vector>& GetNavigationMap() const { return m_NavigationMap; }
    /** @} */

    /**
     * @name Accessors
     * @brief Query tilemap properties.
     * @{
     */
    inline int GetTileWidth() const { return m_TileWidth; }    ///< Tile width in pixels
    inline int GetTileHeight() const { return m_TileHeight; }  ///< Tile height in pixels
    inline int GetMapWidth() const { return m_MapWidth; }      ///< Map width in tiles
    inline int GetMapHeight() const { return m_MapHeight; }    ///< Map height in tiles
    inline const Texture& GetTilesetTexture() const
    {
        return m_TilesetTexture;
    }  ///< Tileset texture
    inline int GetTilesPerRow() const { return m_TilesPerRow; }  ///< Tiles per row in tileset
    inline int GetTilesetDataWidth() const { return m_TilesetDataWidth; }  ///< Tileset image width
    inline int GetTilesetDataHeight() const
    {
        return m_TilesetDataHeight;
    }  ///< Tileset image height
    /** @} */

    /**
     * @name Dynamic Layer System
     * @brief Manage N layers dynamically instead of fixed 6 layers.
     * @{
     */

    /// Get total number of layers
    inline size_t GetLayerCount() const { return m_Layers.size(); }

    /// Get layer by index (0-based)
    TileLayer& GetLayer(size_t index);
    const TileLayer& GetLayer(size_t index) const;

    /// Get/set tile ID for any layer (0-based layer index)
    int GetLayerTile(int x, int y, size_t layer) const;
    void SetLayerTile(int x, int y, size_t layer, int tileID);

    /// Get/set rotation for any layer
    float GetLayerRotation(int x, int y, size_t layer) const;
    void SetLayerRotation(int x, int y, size_t layer, float rotation);

    /// Get/set no-projection flag for any layer
    bool GetLayerNoProjection(int x, int y, size_t layer) const;
    void SetLayerNoProjection(int x, int y, size_t layer, bool noProjection);

    /// Get/set Y-sort-plus flag for any layer
    bool GetLayerYSortPlus(int x, int y, size_t layer) const;
    void SetLayerYSortPlus(int x, int y, size_t layer, bool ySortPlus);

    /// Get/set player-behind flag for any layer (affects Y-sort tiebreaker)
    bool GetLayerYSortMinus(int x, int y, size_t layer) const;
    void SetLayerYSortMinus(int x, int y, size_t layer, bool ySortMinus);

    /**
     * @brief Render all background layers (isBackground == true) in render order.
     * @param renderer Active renderer.
     * @param renderCam Camera position used for rendering offset.
     * @param renderSize Visible area size for rendering.
     * @param cullCam Camera position used for tile culling.
     * @param cullSize Visible area size for tile culling.
     */
    void RenderBackgroundLayers(IRenderer& renderer,
                                glm::vec2 renderCam,
                                glm::vec2 renderSize,
                                glm::vec2 cullCam,
                                glm::vec2 cullSize);

    /**
     * @brief Render all foreground layers (isBackground == false) in render order.
     * @param renderer Active renderer.
     * @param renderCam Camera position used for rendering offset.
     * @param renderSize Visible area size for rendering.
     * @param cullCam Camera position used for tile culling.
     * @param cullSize Visible area size for tile culling.
     */
    void RenderForegroundLayers(IRenderer& renderer,
                                glm::vec2 renderCam,
                                glm::vec2 renderSize,
                                glm::vec2 cullCam,
                                glm::vec2 cullSize);

    /**
     * @brief Render no-projection tiles from all background layers.
     * @param renderer Active renderer.
     * @param renderCam Camera position used for rendering offset.
     * @param renderSize Visible area size for rendering.
     * @param cullCam Camera position used for tile culling.
     * @param cullSize Visible area size for tile culling.
     */
    void RenderBackgroundLayersNoProjection(IRenderer& renderer,
                                            glm::vec2 renderCam,
                                            glm::vec2 renderSize,
                                            glm::vec2 cullCam,
                                            glm::vec2 cullSize);

    /**
     * @brief Render no-projection tiles from all foreground layers.
     * @param renderer Active renderer.
     * @param renderCam Camera position used for rendering offset.
     * @param renderSize Visible area size for rendering.
     * @param cullCam Camera position used for tile culling.
     * @param cullSize Visible area size for tile culling.
     */
    void RenderForegroundLayersNoProjection(IRenderer& renderer,
                                            glm::vec2 renderCam,
                                            glm::vec2 renderSize,
                                            glm::vec2 cullCam,
                                            glm::vec2 cullSize);

private:
    /// Shared implementation for background/foreground no-projection rendering.
    void RenderLayersNoProjection(IRenderer& renderer,
                                  glm::vec2 renderCam,
                                  glm::vec2 renderSize,
                                  glm::vec2 cullCam,
                                  glm::vec2 cullSize,
                                  bool isBackground);

public:
    /**
     * @brief Get layer indices sorted by renderOrder for draw ordering.
     * @return Vector of layer indices sorted ascending by renderOrder.
     */
    std::vector<size_t> GetLayerRenderOrder() const;
    /** @} */

    /**
     * @name No-Projection System
     * @brief Per-tile flag to bypass 3D perspective transformation.
     *
     * Tiles marked with "no projection" will be rendered without 3D perspective
     * distortion, similar to how player/NPC sprites are rendered. This creates
     * the visual effect of objects having height above the ground plane.
     * @{
     */

    /**
     * @brief Get no-projection flag at tile coordinates for a specific layer.
     *
     * @warning This method uses **1-based** layer indexing (unlike the Dynamic
     * Layer System methods which are 0-based). Layer 1 maps to internal layer 0.
     *
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (1-based; internally converted to 0-based).
     * @return true if tile should bypass 3D projection on that layer.
     */
    bool GetNoProjection(int x, int y, int layer = 1) const;

    /**
     * @brief Find the bounding box of the noProjection structure containing the given tile.
     * @param tileX Tile X coordinate.
     * @param tileY Tile Y coordinate.
     * @param outMinX Output: minimum tile X of structure.
     * @param outMaxX Output: maximum tile X of structure.
     * @param outMinY Output: minimum tile Y of structure.
     * @param outMaxY Output: maximum tile Y of structure.
     * @return true if a noProjection structure was found at this tile.
     */
    bool FindNoProjectionStructureBounds(
        int tileX, int tileY, int& outMinX, int& outMaxX, int& outMinY, int& outMaxY) const;

    /**
     * @brief Add a new no-projection structure definition.
     * @param leftAnchor Left anchor world position (corner of tile).
     * @param rightAnchor Right anchor world position (corner of tile).
     * @param name Optional name for editor display.
     * @return The structure ID.
     */
    int AddNoProjectionStructure(glm::vec2 leftAnchor,
                                 glm::vec2 rightAnchor,
                                 const std::string& name = "");

    /**
     * @brief Get a no-projection structure by ID.
     * @param id Structure ID.
     * @return Pointer to structure, or nullptr if invalid ID.
     */
    const NoProjectionStructure* GetNoProjectionStructure(int id) const;

    /**
     * @brief Get all no-projection structures.
     * @return Const reference to structures vector.
     */
    const std::vector<NoProjectionStructure>& GetNoProjectionStructures() const
    {
        return m_NoProjectionStructures;
    }

    /**
     * @brief Remove a no-projection structure by ID.
     *
     * Also clears structureId from all tiles that referenced this structure.
     * @param id Structure ID to remove.
     */
    void RemoveNoProjectionStructure(int id);

    /**
     * @brief Get the structure ID assigned to a tile.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index.
     * @return Structure ID (-1 = auto flood-fill, 0+ = belongs to structure).
     */
    int GetTileStructureId(int x, int y, int layer) const;

    /**
     * @brief Assign a tile to a structure.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index.
     * @param structId Structure ID (-1 = auto flood-fill, 0+ = belongs to structure).
     */
    void SetTileStructureId(int x, int y, int layer, int structId);

    /**
     * @brief Get the number of no-projection structures.
     * @return Number of structures.
     */
    size_t GetNoProjectionStructureCount() const { return m_NoProjectionStructures.size(); }
    /** @} */

    /**
     * @name Elevation System
     * @brief Height offset for stairs and elevated areas.
     *
     * Elevation values are stored per-tile and affect the rendering Y position
     * of entities standing on that tile. Positive values push entities up.
     * @{
     */

    /**
     * @brief Get elevation at tile coordinates.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @return Elevation in pixels (0 = ground level, positive = higher).
     */
    int GetElevation(int x, int y) const;

    /**
     * @brief Set elevation at tile coordinates.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param elevation Elevation in pixels.
     */
    void SetElevation(int x, int y, int elevation);

    /**
     * @brief Get elevation at world position.
     *
     * Maps the world position to the occupied tile using the entity
     * feet-position convention and returns that tile's elevation value.
     *
     * @param worldX World X position in pixels.
     * @param worldY World Y position in pixels.
     * @return Elevation of the mapped tile in pixels.
     */
    float GetElevationAtWorldPos(float worldX, float worldY) const;
    /** @} */

    /**
     * @name Y-Sorted Tile System
     * @brief Per-tile flag for depth-sorted rendering with entities.
     *
     * Tiles marked as Y-sorted are rendered in the same pass as player/NPCs,
     * sorted by Y position. This allows tiles to appear in front of or behind
     * entities based on their relative positions.
     * @{
     */

    /**
     * @brief Data for a Y-sort-plus tile to be rendered in sorted order.
     */
    struct YSortPlusTile
    {
        int x, y;           ///< Tile coordinates
        int layer;          ///< Layer index (0-based)
        float anchorY;      ///< World Y position of tile bottom (for sorting)
        bool noProjection;  ///< True if tile should render without perspective distortion
        bool ySortMinus;    ///< True if player should render behind this tile at same Y
    };

    /**
     * @brief Collect all visible Y-sort-plus tiles for rendering.
     * @param cullCam Camera position for culling.
     * @param cullSize Visible area size for culling.
     * @return Vector of Y-sort-plus tiles within visible range.
     */
    const std::vector<YSortPlusTile>& GetVisibleYSortPlusTiles(glm::vec2 cullCam,
                                                               glm::vec2 cullSize) const;

    /**
     * @brief Render a single tile (for Y-sorted rendering).
     * @param r Active renderer.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (0-based, 0 to layer_count-1).
     * @param cameraPos Camera position in world coordinates.
     * @param useNoProjection Override: -1=auto (read from layer), 0=force normal, 1=force
     * noProjection.
     */
    void RenderSingleTile(
        IRenderer& r, int x, int y, int layer, glm::vec2 cameraPos, int useNoProjection = -1);
    /** @} */

    /**
     * @name Serialization Functions
     * @brief Map persistence using JSON format.
     * @{
     */
    /**
     * @brief Save map to JSON file.
     *
     * Saves all layers, collision, navigation, NPCs, and player position
     * in a compact sparse format.
     *
     * @par JSON Structure
     * @code{.json}
     * {
     *   "width": 64,
     *   "height": 64,
     *   "tileWidth": 16,
     *   "tileHeight": 16,
     *   "dynamicLayers": [
     *     { "name": "Ground", "renderOrder": 0, "tiles": { "index": tileID, ... } },
     *     { "name": "Ground Detail", "renderOrder": 10, "tiles": { ... } },
     *     ...
     *   ],
     *   "collision": [index1, index2, ...],
     *   "navigation": [index1, index2, ...],
     *   "npcs": [
     *     { "type": "BW2_NPC1", "tileX": 10, "tileY": 5, "dialogue": "Hello!" }
     *   ],
     *   "player": { "tileX": 5, "tileY": 5 }
     * }
     * @endcode
     *
     * @param filename Output JSON file path.
     * @param npcs Optional NPC list to save.
     * @param playerTileX Player tile X (-1 to skip).
     * @param playerTileY Player tile Y (-1 to skip).
     * @param characterType Player's character type (-1 to skip).
     * @return `true` if saved successfully.
     */
    bool SaveMapToJSON(const std::string& filename,
                       const std::vector<class NonPlayerCharacter>* npcs = nullptr,
                       int playerTileX = -1,
                       int playerTileY = -1,
                       int characterType = -1) const;

    /**
     * @brief Load map from JSON file.
     *
     * Loads map dimensions, layers, collision/navigation, elevation, and optional
     * NPC/player data from JSON. Returns `false` on file open or parse failures.
     * If dynamic layer indices do not fit the loaded map size, default map data
     * is regenerated for layer content.
     *
     * @param filename Input JSON file path.
     * @param npcs Optional output for loaded NPCs.
     * @param playerTileX Optional output for player X coordinate.
     * @param playerTileY Optional output for player Y coordinate.
     * @param characterType Optional output for player's character type.
     * @return `true` if loaded successfully.
     */
    bool LoadMapFromJSON(const std::string& filename,
                         std::vector<class NonPlayerCharacter>* npcs = nullptr,
                         int* playerTileX = nullptr,
                         int* playerTileY = nullptr,
                         int* characterType = nullptr);
    /** @} */

    /**
     * @name Tileset Utilities
     * @brief Helper functions for tileset operations.
     * @{
     */
    /**
     * @brief Get list of non-transparent tile IDs.
     *
     * Scans the tileset to find tiles with at least one non-transparent pixel.
     * Useful for tile picker UI.
     *
     * @return Vector of valid (non-empty) tile IDs.
     */
    std::vector<int> GetValidTileIDs() const;

    /**
     * @brief Check if a tile is fully transparent.
     *
     * A tile is transparent if all pixels have alpha = 0.
     *
     * @param tileID Tile ID to check.
     * @return `true` if tile is completely transparent.
     */
    bool IsTileTransparent(int tileID) const;
    /** @} */

    /**
     * @name Particle Zones
     * @brief Placeable particle emitter zones for fireflies, rain, snow.
     * @{
     */

    /**
     * @brief Get read-only access to particle zones.
     * @return Const pointer to the particle zones vector.
     */
    const std::vector<ParticleZone>* GetParticleZones() const { return &m_ParticleZones; }

    /**
     * @brief Get mutable access to particle zones.
     * @return Pointer to the particle zones vector.
     */
    std::vector<ParticleZone>* GetParticleZonesMutable() { return &m_ParticleZones; }

    /**
     * @brief Add a new particle zone.
     * @param zone The zone to add.
     */
    void AddParticleZone(const ParticleZone& zone) { m_ParticleZones.push_back(zone); }

    /**
     * @brief Remove a particle zone by index.
     * @param index The index of the zone to remove.
     */
    void RemoveParticleZone(size_t index)
    {
        if (index < m_ParticleZones.size())
        {
            m_ParticleZones.erase(m_ParticleZones.begin() + index);
        }
    }

    /** @} */

    /** @name Animated Tiles
     * @brief Methods for managing animated tile definitions.
     * @{
     */
    /**
     * @brief Add a new animated tile definition.
     * @param anim The animation definition.
     * @return The animation ID (index).
     */
    int AddAnimatedTile(const AnimatedTile& anim)
    {
        m_AnimatedTiles.push_back(anim);
        return static_cast<int>(m_AnimatedTiles.size() - 1);
    }

    /**
     * @brief Get an animated tile definition.
     * @param id Animation ID.
     * @return Pointer to animation, or nullptr if invalid.
     */
    const AnimatedTile* GetAnimatedTile(int id) const
    {
        if (id < 0 || id >= static_cast<int>(m_AnimatedTiles.size()))
            return nullptr;
        return &m_AnimatedTiles[id];
    }

    /**
     * @brief Set a tile position to use an animation.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (0-based).
     * @param animId Animation ID (-1 to clear).
     */
    void SetTileAnimation(int x, int y, int layer, int animId)
    {
        if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
            return;
        if (layer < 0 || layer >= static_cast<int>(m_Layers.size()))
            return;
        size_t idx = static_cast<size_t>(y * m_MapWidth + x);
        if (idx >= m_Layers[layer].animationMap.size())
            return;

        m_Layers[layer].animationMap[idx] = animId;

        // Place the first frame on the layer so there's a tile to render
        // (the animation check happens after tile existence check)
        if (animId >= 0 && animId < static_cast<int>(m_AnimatedTiles.size()) &&
            !m_AnimatedTiles[animId].frames.empty())
        {
            m_Layers[layer].tiles[idx] = m_AnimatedTiles[animId].frames[0];
        }
    }

    /**
     * @brief Get the animation ID for a tile position.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (0-based).
     * @return Animation ID, or -1 if not animated.
     */
    int GetTileAnimation(int x, int y, int layer) const
    {
        if (x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
            return -1;
        if (layer < 0 || layer >= static_cast<int>(m_Layers.size()))
            return -1;
        size_t idx = static_cast<size_t>(y * m_MapWidth + x);
        if (idx >= m_Layers[layer].animationMap.size())
            return -1;
        return m_Layers[layer].animationMap[idx];
    }

    /**
     * @brief Update animation timer.
     * @param deltaTime Time since last update.
     */
    void UpdateAnimations(float deltaTime) { m_AnimationTime += deltaTime; }

    /** @} */

    /**
     * @brief Compute the visible tile range from a camera rectangle, clamped to map bounds.
     * @param mapW Map width in tiles.
     * @param mapH Map height in tiles.
     * @param tileW Tile width in pixels.
     * @param tileH Tile height in pixels.
     * @param cullCam Top-left corner of the visible area in world pixels.
     * @param cullSize Size of the visible area in world pixels.
     * @param[out] x0 First visible tile column (inclusive).
     * @param[out] y0 First visible tile row (inclusive).
     * @param[out] x1 Last visible tile column (inclusive).
     * @param[out] y1 Last visible tile row (inclusive).
     */
    static inline void ComputeTileRange(int mapW,
                                        int mapH,
                                        int tileW,
                                        int tileH,
                                        const glm::vec2& cullCam,
                                        const glm::vec2& cullSize,
                                        int& x0,
                                        int& y0,
                                        int& x1,
                                        int& y1)
    {
        float minX = cullCam.x;
        float minY = cullCam.y;
        float maxX = cullCam.x + cullSize.x;
        float maxY = cullCam.y + cullSize.y;

        x0 = (int)std::floor(minX / tileW);
        y0 = (int)std::floor(minY / tileH);
        x1 = (int)std::floor(maxX / tileW);
        y1 = (int)std::floor(maxY / tileH);

        // clamp to map
        x0 = std::max(0, std::min(x0, mapW - 1));
        y0 = std::max(0, std::min(y0, mapH - 1));
        x1 = std::max(0, std::min(x1, mapW - 1));
        y1 = std::max(0, std::min(y1, mapH - 1));
    }

    /**
     * @name Array Access Operators
     * @brief 2D array syntax for Ground layer tiles (index 0).
     *
     * Provides convenient `tilemap[x][y]` access pattern.
     * @{
     */
    /**
     * @brief Access Ground layer tiles (mutable).
     *
     * @code{.cpp}
     * tilemap[10][20] = 15;  // Set tile at (10,20) to ID 15
     * @endcode
     *
     * @param x Tile column.
     * @return ColumnProxy for row access.
     */
    ColumnProxy<std::vector<int>, int, -1> operator[](int x)
    {
        return ColumnProxy<std::vector<int>, int, -1>(
            &m_Layers[0].tiles, &m_MapWidth, &m_MapHeight, x);
    }

    /**
     * @brief Access Ground layer tiles (const).
     *
     * @code{.cpp}
     * int tileID = tilemap[10][20];
     * @endcode
     *
     * @param x Tile column.
     * @return ColumnProxy for const-qualified access.
     */
    ConstColumnProxy<std::vector<int>, int, -1> operator[](int x) const
    {
        return ConstColumnProxy<std::vector<int>, int, -1>(
            &m_Layers[0].tiles, &m_MapWidth, &m_MapHeight, x);
    }
    /** @} */

private:
    /// @name Tileset
    /// @{
    Texture m_TilesetTexture;                     ///< Combined tileset texture
    int m_TileWidth, m_TileHeight;                ///< Tile dimensions in pixels
    int m_TilesetWidth, m_TilesetHeight;          ///< Tileset dimensions in tiles
    int m_TilesPerRow;                            ///< Tiles per row in tileset
    unsigned char* m_TilesetData;                 ///< Raw image data for transparency checks
    int m_TilesetDataWidth, m_TilesetDataHeight;  ///< Raw image dimensions
    int m_TilesetChannels;                        ///< Number of color channels (3=RGB, 4=RGBA)
    bool m_TilesetDataFromStbi;  ///< True if allocated by stbi_load, false if by new[]
    std::vector<bool> m_TileTransparencyCache;  ///< Cached transparency results per tile ID
    bool m_TransparencyCacheBuilt;              ///< Whether the cache has been built
    /// @}

    /// @name Map Dimensions
    /// @{
    int m_MapWidth, m_MapHeight;  ///< Map dimensions in tiles
    /// @}

    /// @name Dynamic Layers
    /// @{
    std::vector<TileLayer> m_Layers;  ///< All tile layers (10 layers: 5 background, 5 foreground)
    /// @}

    /// @name Collision and Navigation
    /// @{
    CollisionMap<std::vector> m_CollisionMap;    ///< Collision flags
    NavigationMap<std::vector> m_NavigationMap;  ///< NPC walkability flags
    std::vector<uint8_t>
        m_CornerCutBlocked;  ///< Per-tile corner cut disable mask (4 bits per tile)
    /// @}

    /// @name Elevation Data
    /// @{
    std::vector<int> m_Elevation;  ///< Per-tile elevation in pixels (0 = ground)
    /// @}

    /// @name Particle Zones
    /// @{
    std::vector<ParticleZone> m_ParticleZones;  ///< Placeable particle emitter zones
    /// @}

    /// @name Animated Tiles
    /// @{
    std::vector<AnimatedTile> m_AnimatedTiles;  ///< Animation definitions
    std::vector<int> m_TileAnimationMap;        ///< Per-tile animation ID (-1 = not animated)
    float m_AnimationTime;                      ///< Global animation timer
    /// @}

    /// @name No-Projection Structures
    /// @{
    std::vector<NoProjectionStructure>
        m_NoProjectionStructures;  ///< Manually defined structures with anchors
    /// @}

    /// @name Render Cache (reused each frame to avoid allocations)
    /// @{
    mutable std::vector<YSortPlusTile>
        m_YSortPlusTilesCache;                   ///< Cached Y-sort tiles (reused each frame)
    mutable std::vector<bool> m_ProcessedCache;  ///< Cached processed flags (reused each frame)
    mutable std::vector<bool>
        m_RenderedStructuresCache;  ///< Cached structure flags (reused each frame)
    /// @}

    /**
     * @brief Generate default tile content.
     *
     * Scans the loaded tileset for non-transparent tiles and fills the base
     * layer with randomly selected valid tile IDs.
     * Called by SetTilemapSize() when generateMap is true.
     */
    void GenerateDefaultMap();

    /**
     * @brief Build the transparency cache for all tiles.
     *
     * Pre-computes transparency for every tile ID to avoid expensive
     * per-pixel checks during rendering. Called on tileset load.
     */
    void BuildTransparencyCache();
};
