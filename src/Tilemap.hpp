#pragma once

#include "CameraRig.hpp"
#include "CollisionMap.hpp"
#include "ColumnProxy.hpp"
#include "DefaultedVector.hpp"
#include "ElevationAxis.hpp"
#include "ElevationRole.hpp"
#include "IRenderer.hpp"
#include "NavigationMap.hpp"
#include "ParticleSystem.hpp"
#include "SupportSurface.hpp"
#include "Texture.hpp"
#include "TileMath.hpp"
#include "TileStance.hpp"
#include "WeatherDefinitions.hpp"

#include <ecs.hpp>

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @struct Tile
 * @brief Represents a single tile's position in the tileset texture.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Not wired into the engine. Layers store bare integer tile ids, and every
 * tileset index and UV computation derives (tileX, tileY) from the id inline.
 */
struct Tile
{
    int tileX;   ///< Column in tileset (0-based).
    int tileY;   ///< Row in tileset (0-based).
    int tileID;  ///< Unique identifier (tileY * tilesPerRow + tileX).
};

/**
 * @struct NoProjectionStructure
 * @brief Defines a no-projection structure with manually placed anchors.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Structures are groups of tiles that bypass 3D projection. Instead of
 * automatic flood-fill detection, structures are manually defined with
 * explicit anchor positions for precise alignment control.
 */
struct NoProjectionStructure
{
    int id;                 ///< Unique structure ID (0+).
    std::string name;       ///< Optional name for editor display.
    glm::vec2 leftAnchor;   ///< Left anchor world position (click corner of tile).
    glm::vec2 rightAnchor;  ///< Right anchor world position (click corner of tile).

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
 * @ingroup World
 *
 * Each layer contains tile IDs, rotation values, and per-tile flags.
 * Layers are rendered in order based on their renderOrder value.
 */
struct TileLayer
{
    std::string name;                        ///< Human-readable layer name.
    defaulted_vector<int, -1> tiles;         ///< Tile IDs in row-major order (-1 = empty).
    defaulted_vector<float, 0.0f> rotation;  ///< Rotation in degrees per tile.
    defaulted_vector<TileStance, TileStance::Flat>
        stance;  ///< Ground vs upright role (see TileStance).
    defaulted_vector<ElevationRole, ElevationRole::Ground>
        elevationRole;                    ///< Per-layer participation in the cell's elevation.
    defaulted_vector<bool, false> flipX;  ///< Mirror tile sprite around vertical axis.
    defaulted_vector<bool, false> flipY;  ///< Mirror tile sprite around horizontal axis.
    defaulted_vector<int, -1>
        structureId;  ///< Per-tile structure ID (-1 = auto flood-fill, 0+ = belongs to structure).
    defaulted_vector<bool, false> ySortPlus;  ///< Tiles that sort with entities by Y position.
                                              ///< (Y-sort+1: player in front at same Y).
    defaulted_vector<bool, false>
        ySortMinus;  ///< When true, player renders behind tile at same Y (Y-sort-1: tile in front).
    defaulted_vector<int, -1> animationMap;  ///< Per-tile animation ID (-1 = not animated).
    int renderOrder;    ///< Lower = rendered first (background), higher = later (foreground).
    bool isBackground;  ///< true = before player/NPCs, false = after.

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

    /**
     * @brief Resize all per-tile fields to the given number of tiles.
     * @param size Total number of tiles (mapWidth * mapHeight).
     */
    void Resize(size_t size)
    {
        resize_all(size,
                   tiles,
                   rotation,
                   stance,
                   elevationRole,
                   flipX,
                   flipY,
                   structureId,
                   ySortPlus,
                   ySortMinus,
                   animationMap);
    }

    /// @brief Reset all per-tile data to default values without changing size.
    void Clear()
    {
        reset_all(tiles,
                  rotation,
                  stance,
                  elevationRole,
                  flipX,
                  flipY,
                  structureId,
                  ySortPlus,
                  ySortMinus,
                  animationMap);
    }
};

/**
 * @struct AnimatedTile
 * @brief Definition of an animated tile sequence.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 */
struct AnimatedTile
{
    std::vector<int> frames;  ///< Tile IDs for each frame.
    float frameDuration;      ///< Seconds per frame.

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
 * @ingroup World
 *
 * The world container. One Tilemap holds everything the map authors and everything the render,
 * collision and navigation paths read:
 * - **A dynamic tile-layer stack** (10 by default: 5 background, 5 foreground) with
 *   configurable depth ordering
 * - **Collision detection** for player movement
 * - **Navigation mesh** for NPC pathfinding
 * - **Per-tile rotation** for visual variety
 * - **Elevation and corner-cut flags** for ramps, ledges, and collision tuning
 * - **No-projection structures** for upright buildings and attached effects
 * - **Y-sort flags** for tiles that interleave with entities by screen Y
 * - **Particle zones, world lights, and animated tiles** authored by the editor
 * - **JSON serialization** using a `dynamicLayers` format
 *
 * @par Layer architecture
 * The layer count is data-driven, not an invariant. The constructor and
 * @ref SetTilemapSize build the default 10-layer stack shown below, but
 * @ref LoadMapFromJSON clears the stack and rebuilds it from the map's
 * `dynamicLayers[]` array, so a loaded map may have any number of layers.
 * @ref GetLayerCount is the only authority - do not hard-code 10.
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
 * @par Depth sorting visualization
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
 * A layer's side of the actors comes from its `isBackground` flag, not from its index:
 * in the default stack layers 0-4 are background (drawn before player/NPCs) and 5-9 are
 * foreground (drawn after), so characters walk behind 5-9 and in front of 0-4.
 *
 * @par Tile ID system
 * Tile IDs map directly to tileset positions:
 * @f[
 * tileID = tileY \times tilesPerRow + tileX
 * @f]
 *
 * Where (tileX, tileY) are the tile's coordinates in the tileset texture.
 * A tileID of -1 represents an empty/transparent tile.
 *
 * @par UV coordinate calculation
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
 * @par Coordinate system
 * The tilemap uses a top-left origin with Y increasing downward:
 * @code
 *   (0,0)-----> +X
 *     |
 *     |  Tile (x,y) at world position (x*tileWidth, y*tileHeight)
 *     v
 *    +Y
 * @endcode
 * Tile size is not fixed at 16px: it comes from the project manifest
 * (`tileWidth` / `tileHeight`) and is readable via @ref GetTileWidth / @ref GetTileHeight.
 *
 * @par World-to-tile conversion
 * Bare grid mapping, for a point that is already a tile-space sample:
 * @f[
 * tile_x = \lfloor \frac{world_x}{tileWidth} \rfloor, \quad
 * tile_y = \lfloor \frac{world_y}{tileHeight} \rfloor
 * @f]
 * Entity positions are bottom-center feet anchors, so every entity-facing query
 * (@ref WorldToTileCoord, @ref GetElevationAtWorldPos) shifts Y up by half a tile
 * first, which keeps a feet position on a tile's bottom edge inside that tile:
 * @f[
 * tile_y = \left\lfloor \frac{world_y - \frac{tileHeight}{2}}{tileHeight} \right\rfloor
 * @f]
 * Do not copy the bare form into entity code; it is off by one row. @ref TileMath
 * holds all three row conventions and is the only place they are defined.
 *
 * @par Tile-to-world conversion
 * @f[
 * world_x = tile_x \times tileWidth, \quad
 * world_y = tile_y \times tileHeight
 * @f]
 *
 * @par Memory layout
 * Each layer stores tiles in row-major order:
 * @f[
 * index = y \times mapWidth + x
 * @f]
 *
 * @par Tileset combination
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
 * @par Sparse storage format
 * Each entry of the `dynamicLayers` array stores every per-tile field sparsely, keyed by
 * the row-major flat index `i = y * mapWidth + x`. Value fields (`tiles`, `rotation`,
 * `stance`, `elevationRole`, `structureId`) are objects keyed by index and hold only
 * non-default cells; boolean fields (`flipX`, `flipY`, `ySortPlus`, `ySortMinus`) are arrays
 * listing the indices that are set. `stance`, `elevationRole` and `structureId` are the
 * per-layer keys omitted entirely when they would be empty; per-tile animation ids are not
 * stored here but in the top-level `layerAnimationMaps` array (one object per layer).
 *
 * `stance` holds @ref TileStance as an integer (0 Flat, 1 Prop, 2 Wall, 3 Structure). Maps
 * written before it existed carry a `noProjection` index array instead; @ref LoadMapFromJSON
 * migrates those, and the key is never written again.
 *
 * `elevationRole` holds @ref ElevationRole as an integer (0 Ground, 1 Raised,
 * 2 Ramp) and decides which layers rise to the cell's `elevation`. A map written without that
 * key loads as all-Ground and renders unchanged, so unlike `stance` it needs no migration.
 * @code{.json}
 * {
 *   "dynamicLayers": [
 *     {
 *       "name": "Ground",
 *       "renderOrder": 0,
 *       "isBackground": true,
 *       "tiles": {
 *         "42": 15,    // Tile at index 42 = tile ID 15
 *         "100": 23    // Tile at index 100 = tile ID 23
 *       },
 *       "rotation": { "42": 90.0 },
 *       "stance": { "42": 3 },
 *       "elevationRole": { "42": 1 },
 *       "flipX": [42],
 *       "flipY": [],
 *       "ySortPlus": [],
 *       "ySortMinus": [],
 *       "structureId": { "42": 0 }
 *     }
 *   ]
 * }
 * @endcode
 *
 * This significantly reduces file size for large, sparse maps. @ref SaveMapToJSON
 * documents the surrounding top-level document (grids, structures, effects, actors).
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

    /// @brief Destructor releases tileset texture resources.
    ~Tilemap();

    /**
     * @brief Tilemap is move-only (owns GPU textures and tileset data).
     * Defaulted: all data members are moveable; mutable caches default-construct
     * as empty and rebuild lazily (m_StructureBoundsCacheDirty NSDMI = true).
     */
    Tilemap(Tilemap&&) noexcept = default;
    Tilemap& operator=(Tilemap&&) noexcept = default;
    Tilemap(const Tilemap&) = delete;
    Tilemap& operator=(const Tilemap&) = delete;

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

    /// One sheet to pack into the atlas.
    struct AtlasPackEntry
    {
        std::string key;         ///< Identifier used to look the offset back up.
        const Texture* texture;  ///< Borrowed for the call; null entries are skipped.
    };

    /**
     * @brief Append character sprite sheets into the tile atlas after load.
     *
     * Rebuilds the combined atlas with each sheet appended at the bottom
     * and re-uploads to GPU. Each sheet is identified by a string key
     * (e.g., NPC type, "player_walk") so the renderer can resolve its
     * pixel offset within the atlas via @ref GetCharacterAtlasOffset.
     *
     * This is the mechanism for collapsing the Y-sorted pass into one
     * draw: NPCs and the player draw their sprites out of the same
     * texture as the tiles, eliminating per-NPC texture switches.
     *
     * @warning Each call replaces the previously packed set. The atlas is first
     *          truncated back to the tileset-only baseline and every prior key is
     *          discarded, so a second call that omits an earlier sheet leaves that
     *          sheet unresolvable. Pass the complete set in one call; an empty
     *          vector shrinks the atlas back to tileset-only.
     *
     * @param sheets @ref AtlasPackEntry rows. Null textures are skipped
     *               silently. Each remaining texture is copied during the
     *               call and is not retained. All sheets must have the same
     *               channel count as the atlas and fit within its width.
     * @return `true` on success; `false` if dimensions/channels are
     *         incompatible or the GPU re-upload fails.
     */
    bool PackAdditionalSheets(const std::vector<AtlasPackEntry>& sheets);

    /**
     * @brief Look up the atlas offset of a packed character sheet.
     * @param key Identifier used when @ref PackAdditionalSheets was called.
     * @return Offset of the sheet's first GL row, in pixels measured from the
     *         atlas bottom (x is always 0). Characters add their sprite
     *         coordinates to `y`; atlas-rect samplers use it as the rect
     *         origin. @c nullopt when the key was never packed.
     */
    std::optional<glm::vec2> GetCharacterAtlasOffset(const std::string& key) const;

    /**
     * @brief Set the tilemap dimensions.
     *
     * Allocates storage for all layers, collision, and navigation.
     * Optionally generates a default map pattern.
     *
     * @par World size
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
     * @name Corner cutting control
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
        CORNER_TL = 0,  ///< Top-left corner.
        CORNER_TR = 1,  ///< Top-right corner.
        CORNER_BL = 2,  ///< Bottom-left corner.
        CORNER_BR = 3   ///< Bottom-right corner.
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
     * @name Collision functions
     * @brief Per-cell collision flags for player movement.
     *
     * Collision is one boolean grid per map cell, held beside the layer stack - it is not
     * a property of any layer. @ref SetTileCollision writes that single grid, so a cell
     * either blocks or does not regardless of which layers carry artwork there, and layer 0
     * has no special role. Nor are the other layers decorative: they drive Y-sort
     * (`ySortPlus` / `ySortMinus`), no-projection structures, and elevated surface artwork.
     * @verbatim
     *   per-cell grids (one bool per cell)      layer stack (GetLayerCount() layers)
     *
     *      Collision      Navigation              layer N-1 [tiles, rotation, flags, ...]
     *      +--+--+--+     +--+--+--+                  ...
     *      |  |##|  |     |##|##|  |              layer 1   [tiles, rotation, flags, ...]
     *      +--+--+--+     +--+--+--+              layer 0   [tiles, rotation, flags, ...]
     *      |##|##|  |     |  |##|##|
     *      +--+--+--+     +--+--+--+       one (x, y) indexes every grid and every layer
     * @endverbatim
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
     * @name Navigation functions
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
    inline int GetTileWidth() const { return m_TileWidth; }    ///< Tile width in pixels.
    inline int GetTileHeight() const { return m_TileHeight; }  ///< Tile height in pixels.
    inline int GetMapWidth() const { return m_MapWidth; }      ///< Map width in tiles.
    inline int GetMapHeight() const { return m_MapHeight; }    ///< Map height in tiles.

    /**
     * @brief Flatten (x,y) to a row-major size_t index. Promotes to size_t
     * before the multiplication so the product never overflows `int` on
     * very large maps. Caller is responsible for bounds.
     */
    inline size_t FlatIndex(int x, int y) const
    {
        return static_cast<size_t>(y) * static_cast<size_t>(m_MapWidth) + static_cast<size_t>(x);
    }

    /**
     * @brief Total cell count (width * height) as size_t without intermediate
     * int overflow.
     */
    inline size_t MapCellCount() const
    {
        return static_cast<size_t>(m_MapWidth) * static_cast<size_t>(m_MapHeight);
    }
    inline const Texture& GetTilesetTexture() const
    {
        return m_TilesetTexture;
    }  ///< Tileset texture.
    inline int GetTilesPerRow() const { return m_TilesPerRow; }  ///< Tiles per row in tileset.
    /// Combined atlas width in pixels.
    inline int GetTilesetDataWidth() const { return m_TilesetDataWidth; }
    /// Combined atlas height in pixels, including any sheets added by
    /// @ref PackAdditionalSheets.
    inline int GetTilesetDataHeight() const { return m_TilesetDataHeight; }
    /** @} */

    /**
     * @name Dynamic layer system
     * @brief Manage the layer vector dynamically while preserving
     * the default 10-layer layout.
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

    /// Get/set the authored ground-vs-upright stance for any layer
    TileStance GetLayerStance(int x, int y, size_t layer) const;
    void SetLayerStance(int x, int y, size_t layer, TileStance stance);

    /// Get/set whether this layer's artwork rises to the cell's elevation
    ElevationRole GetLayerElevationRole(int x, int y, size_t layer) const;
    void SetLayerElevationRole(int x, int y, size_t layer, ElevationRole role);

    /// Get/set per-tile horizontal flip (mirror around vertical axis)
    bool GetLayerFlipX(int x, int y, size_t layer) const;
    void SetLayerFlipX(int x, int y, size_t layer, bool flipX);

    /// Get/set per-tile vertical flip (mirror around horizontal axis)
    bool GetLayerFlipY(int x, int y, size_t layer) const;
    void SetLayerFlipY(int x, int y, size_t layer, bool flipY);

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
     * @param renderSize Currently unused; reserved for call-site symmetry. Extent
     *                   comes from @p cullCam / @p cullSize, the draw offset from
     *                   @p renderCam. Changing it has no effect.
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
     * @param renderSize Currently unused; reserved for call-site symmetry. Extent
     *                   comes from @p cullCam / @p cullSize, the draw offset from
     *                   @p renderCam. Changing it has no effect.
     * @param cullCam Camera position used for tile culling.
     * @param cullSize Visible area size for tile culling.
     */
    void RenderForegroundLayers(IRenderer& renderer,
                                glm::vec2 renderCam,
                                glm::vec2 renderSize,
                                glm::vec2 cullCam,
                                glm::vec2 cullSize);

    /**
     * @brief Render every tile layer as world-space 3D geometry.
     *
     * The 3D counterpart to @ref RenderBackgroundLayers +
     * @ref RenderForegroundLayers + the no-projection passes, which it replaces
     * wholesale: one traversal emits both the flat ground quads and the upright
     * billboards, because a depth buffer - not draw order - now resolves which
     * is in front. That is why there is a single method here where the flat
     * pipeline needed four.
     *
     * Tiles are classified by their authored @ref TileStance through
     * @ref tileRole::IsUpright; legacy maps are migrated once at load.
     *
     * @param renderer Active renderer; geometry is submitted via DrawQuad3D.
     * @param rig      Camera parameters, used for the visible ground footprint
     *                 and for billboard orientation.
     */
    void RenderWorld3D(IRenderer& renderer, const cameraRig::RigParams& rig);

    /**
     * @brief Bottom-most row of the contiguous vertical run of TileStance::Structure
     *        tiles that (@p tileX, @p tileY) belongs to.
     *
     * Multi-tile structures are authored as a vertical run of tiles - in the
     * flat top-down view those rows paint one above the other and read as one
     * tall image. In 3D the whole run has to stand on a single ground row, or
     * its tiles end up at different depths instead of stacked. This finds that
     * row by walking south while the tiles stay non-empty and Structure.
     *
     * Only Structure cells continue the run, so a fence post sitting directly
     * south of a building cannot become that building's base. A lone tile
     * returns its own row.
     *
     * @param layer Layer to scan; runs never cross layers, matching authoring.
     * @param tileX Column.
     * @param tileY Row to scan down from.
     * @return Row index of the run's base; never less than @p tileY.
     */
    int FindStructureBaseRow(const TileLayer& layer, int tileX, int tileY) const;

    /**
     * @brief Column span of the contiguous horizontal run of TileStance::Structure
     *        tiles that (@p tileX, @p tileY) belongs to.
     *
     * The horizontal counterpart of @ref FindStructureBaseRow, and needed for the
     * same reason in the other axis. A narrow structure turns toward the camera
     * about its own center, so two of its tiles standing side by side would pivot
     * about different axes and their shared edge would split open as soon as the
     * yaw left zero - a two-tile log visibly breaking in half as the camera
     * moves. Knowing the run lets the whole body rotate as one rigid quad, with
     * each tile placed as a slice along the shared right axis. The span also
     * feeds @ref tileRole::DampingForWidth, which is what makes a wide facade
     * hold its footprint while a one-wide tower still turns.
     *
     * A lone tile returns its own column for both bounds.
     *
     * Only called for Structure artwork, and only Structure cells continue the
     * run, so a Wall or a Prop standing beside a building is never absorbed into
     * it and frozen with its pivot.
     *
     * @param layer    Layer to scan; runs never cross layers.
     * @param tileX    Column to scan out from.
     * @param tileY    Row.
     * @param outMinX  Receives the westmost column of the run.
     * @param outMaxX  Receives the eastmost column of the run.
     */
    void FindStructureRunColumns(
        const TileLayer& layer, int tileX, int tileY, int& outMinX, int& outMaxX) const;

    /**
     * @brief The scene heights of one cell's two edges along its slope axis.
     *
     * @c minus is the west or north edge, @c plus the east or south edge; they are
     * equal for a level cell.
     */
    struct SurfaceSlope
    {
        float minus = 0.0f;   ///< Height at the low-coordinate edge.
        float plus = 0.0f;    ///< Height at the high-coordinate edge.
        bool alongZ = false;  ///< true when the slope runs north-south.
    };

    /**
     * @brief Resolve a cell's surface heights for one layer.
     *
     * Reads the cell's elevation and this layer's @ref ElevationRole; for a Ramp it
     * also reads the two neighbours along @ref GetElevationAxisAt, on the same
     * layer. Reading a neighbour's raw cell elevation instead would let a layer
     * that is not participating pull the ramp up.
     *
     * @param layer Layer to read roles from; runs never cross layers.
     * @param tileX Column.
     * @param tileY Row.
     */
    SurfaceSlope ResolveSurfaceSlope(const TileLayer& layer, int tileX, int tileY) const;

    /**
     * @brief Render no-projection tiles from all background layers.
     * @param renderer Active renderer.
     * @param renderCam Camera position used for rendering offset.
     * @param renderSize Currently unused; reserved for call-site symmetry. Extent
     *                   comes from @p cullCam / @p cullSize, the draw offset from
     *                   @p renderCam. Changing it has no effect.
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
     * @param renderSize Currently unused; reserved for call-site symmetry. Extent
     *                   comes from @p cullCam / @p cullSize, the draw offset from
     *                   @p renderCam. Changing it has no effect.
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
    /// `renderSize` is accepted for signature symmetry only and is never read.
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
     * @name No-projection system
     * @brief Per-tile flag to bypass 3D perspective transformation.
     *
     * Tiles marked with "no projection" will be rendered without 3D perspective
     * distortion, similar to how player/NPC sprites are rendered. This creates
     * the visual effect of objects having height above the ground plane.
     * @{
     */

    /**
     * @brief Whether the tile at these coordinates is part of an upright structure.
     *
     * @warning This method uses **1-based** layer indexing (unlike the Dynamic
     * Layer System methods which are 0-based). Layer 1 maps to internal layer 0.
     * The 1-based convention is a legacy artifact preserved here so default
     * callers (`layer = 1`) keep targeting the Ground layer without churn.
     * Prefer the 0-based layer accessors (e.g. `GetLayerStance`) in new code;
     * this overload is kept only for backward compatibility.
     *
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (1-based; internally converted to 0-based).
     * @return true if the tile's stance is TileStance::Structure on that layer.
     */
    bool IsStructureTile(int x, int y, int layer = 1) const;

    /**
     * @brief Bounding box of the 4-connected TileStance::Structure region containing a tile.
     *
     * A cell joins the region when any layer marks it TileStance::Structure, so the
     * box is the union across the whole layer stack. Authored `structureId` values
     * are not consulted: two adjacent authored structures return one box, and a
     * structure cell with no id still returns one. The id-aware extent of a single
     * authored structure comes from the internal structure-bounds cache instead.
     *
     * @warning Scratches an internal flood-fill buffer, so the call is not reentrant
     *          and not safe to run concurrently on one Tilemap.
     *
     * @param tileX Tile X coordinate.
     * @param tileY Tile Y coordinate.
     * @param outMinX Output: minimum tile X of the region.
     * @param outMaxX Output: maximum tile X of the region.
     * @param outMinY Output: minimum tile Y of the region.
     * @param outMaxY Output: maximum tile Y of the region.
     * @return true when the starting tile is a Structure cell on some layer.
     */
    bool FindNoProjectionStructureBounds(
        int tileX, int tileY, int& outMinX, int& outMaxX, int& outMinY, int& outMaxY) const;

    /**
     * @brief Map a world-space point onto an upright structure's surface.
     *
     * Uses the same stepped shared-edge mesh math as upright structure tile
     * rendering, so attached effects (for example particles) stay locked to the
     * structure's face. An upright structure stands on its anchor base and
     * extrudes upward, so a point inside its footprint lands on that face rather
     * than on the ground beneath it - this is not the identity mapping.
     *
     * @param worldPos World position in pixels.
     * @param cameraPos Camera world position in pixels.
     * @param[out] outScreenPos Output screen-space position.
     * @return `true` when the point was mapped onto a matching structure; `false`
     *         when no structure covers the point and callers should fall back to
     *         plain placement.
     */
    bool ProjectNoProjectionStructurePoint(const glm::vec2& worldPos,
                                           const glm::vec2& cameraPos,
                                           glm::vec2& outScreenPos) const;

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
     * @brief Insert a no-projection structure at a specific index, shifting
     * existing structures with id >= idx upward by one.
     *
     * Inverse of RemoveNoProjectionStructure for the structure-removal-undo
     * use case (the editor's RemoveStructureCmd needs to put the structure
     * back at its original index so per-tile structureId references that
     * were captured before removal still align). The shifted structures get
     * their ids re-stamped to match their new vector positions.
     *
     * @param idx Insertion index. Must be <= current vector size.
     * @param structure Structure data to insert (id field is overwritten).
     */
    void InsertNoProjectionStructureAt(size_t idx, const NoProjectionStructure& structure);

    /**
     * @brief Get the structure ID assigned to a tile.
     *
     * @warning Like @ref IsStructureTile, this pair uses **1-based** layer indexing: it
     * reads internal layer `layer - 1`, so `layer = 1` is the Ground layer and `layer = 0`
     * always returns -1. The 0-based `GetLayer*` accessors do not share this convention.
     *
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (1-based; internally converted to 0-based).
     * @return Structure ID (-1 = auto flood-fill, 0+ = belongs to structure).
     */
    int GetTileStructureId(int x, int y, int layer) const;

    /**
     * @brief Assign a tile to a structure.
     *
     * @warning 1-based layer indexing, matching @ref GetTileStructureId - see the warning
     * there. A 0-based index silently writes the layer below, and index 0 writes nothing.
     *
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (1-based; internally converted to 0-based).
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
     * @name Elevation system
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
     * @brief Connected non-zero-elevation region containing a tile.
     *
     * Region IDs are runtime-only and may change after elevation editing. A
     * negative result means the tile is ordinary ground. Rendering uses this
     * identity to apply under/on-surface ordering only to the structure whose
     * footprint actually contains an actor.
     */
    int GetElevationRegionId(int x, int y) const;

    /// @brief Elevation-region query using the bottom-center feet convention.
    int GetElevationRegionIdAtWorldPos(float worldX, float worldY) const;

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

    /**
     * @brief Convert a world-space feet position to (tileX, tileY).
     *
     * Uses the bottom-center entity convention: Y is shifted up by half a
     * tile so a feet position resting on the bottom edge of tile N maps
     * to tile N (rather than the boundary between N and N+1).
     */
    inline void WorldToTileCoord(float worldX, float worldY, int& tileX, int& tileY) const
    {
        tileX = TileMath::TileIndex(worldX, static_cast<float>(m_TileWidth));
        tileY = TileMath::AnchorTileRow(worldY, static_cast<float>(m_TileHeight));
    }

    /**
     * @brief Auto-derive the elevation engagement axis for a tile.
     *
     * The axis identifies which tile edge can connect ground to this elevated
     * surface. The derivation runs in order and stops at the first decision:
     *
     * 1. Elevation 0 -> ElevationAxis::None.
     * 2. Compare the orthogonal gradients |eE - eW| and |eN - eS|; the larger
     *    one wins. This is the primary signal, because a ramp keeps a strong
     *    gradient on its own axis even when same-height neighbours flank it.
     * 3. On a gradient tie (deck interior, isolated platform, symmetric cross),
     *    walk outward up to 8 cells in each cardinal direction and count
     *    contiguous elevated cells; the axis with the longer span wins.
     * 4. Still tied (a symmetric platform) -> ElevationAxis::X.
     *
     * Cost: 4 neighbour reads in the common case, up to 36 when the gradient
     * ties. Safe to call on out-of-bounds tiles.
     *
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @return ElevationAxis::None, ElevationAxis::X or ElevationAxis::Y.
     */
    ElevationAxis GetElevationAxisAt(int x, int y) const;

    /** @} */

    /**
     * @name World depth tile system
     * @brief Explicit Y-sort and inferred elevated tiles rendered with entities.
     *
     * Authored layer role and Y-sort remain the baseline. Connected elevation
     * region identity supplies only structure-local underpass/deck constraints.
     *
     * Tilemap owns only the first two stages: it decides which tiles are promoted
     * and fills the payload. The depth key itself is `anchorY` alone, and the
     * remaining fields reach the sorter as metadata for the surface-local pass.
     *
     * @htmlonly
     * <pre class="mermaid">
     * flowchart TD
     *     Gate["IsDepthSortedTile: ySortPlus, or layer 2+ with elevation"]
     *     Build["GetVisibleDepthSortedTiles: anchorY = stack bottom edge"]
     *     Item["Game: Drawable.sortY = anchorY only"]
     *     Sort["SortDrawables: phase, then sortY, then per-region edges"]
     *     Gate --> Build
     *     Build -- "supportHeight + surfaceRegionId are metadata, not depth" --> Item
     *     Item --> Sort
     * </pre>
     * @endhtmlonly
     * @{
     */

    /**
     * @brief Tile promoted from a fixed layer into the world depth queue.
     *
     * Explicit Y-sort tiles and tiles belonging to an elevated support surface
     * share this payload. @ref surfaceRegionId associates overhanging artwork
     * with one connected elevation footprint without globally reordering it.
     */
    struct DepthSortedTile
    {
        int x = 0, y = 0;      ///< Tile coordinates.
        int layer = 0;         ///< Layer index (0-based).
        float anchorY = 0.0f;  ///< World Y position of the tile/stack bottom.
        /// Inherited surface elevation in pixels. Support metadata only; it is
        /// never folded into the painter-depth key (@ref Drawable::supportHeight).
        float supportHeight = 0;
        SupportSurface supportSurface{
            SupportSurface::Ground};  ///< Logical surface the artwork belongs to.
        int surfaceRegionId = -1;     ///< Connected elevation footprint, or -1.
        bool authoredYSort = false;   ///< Copy of the cell's authored ySortPlus flag.
        bool isBackground = true;     ///< Original fixed-pass side of actors.
        bool isStructure = false;     ///< Render upright, without perspective distortion.
        bool ySortMinus = false;      ///< Tile wins near-depth entity comparisons.
    };

    /**
     * @brief Whether a tile is owned by the unified world depth queue.
     *
     * A tile is promoted when its `ySortPlus` flag is set, or when its layer index
     * is >= 2 (the first object layer) and the cell's elevation is non-zero, which
     * treats object/foreground artwork over elevation as the visible elevated
     * surface. Layers 0 and 1 (ground and ground detail) stay in the fixed passes
     * below it, so existing maps need no new authoring.
     *
     * @note `ySortMinus` never promotes a tile. It is only a tiebreaker applied to
     *       tiles already in the queue, so authoring it alone on a non-promoted
     *       tile has no effect.
     */
    bool IsDepthSortedTile(int x, int y, size_t layer) const;

    /**
     * @brief Collect all visible explicit/elevated tiles for depth sorting.
     *
     * @warning The returned reference aliases an internal per-frame cache. The next
     *          call clears and rebuilds it, so two results cannot be held at once.
     *          Copy the contents when they must outlive one call.
     *
     * @param cullCam Camera position for culling.
     * @param cullSize Visible area size for culling.
     * @return Reused vector of depth-sorted tiles. The scanned range is the cull
     *         rectangle expanded by 8 tiles on every side, so tiles just outside
     *         the visible rectangle are included on purpose - a tall structure
     *         anchored off-screen still contributes its artwork.
     */
    const std::vector<DepthSortedTile>& GetVisibleDepthSortedTiles(glm::vec2 cullCam,
                                                                   glm::vec2 cullSize) const;

    /**
     * @brief Render a single tile (for Y-sorted rendering).
     * @param r Active renderer.
     * @param x Tile X coordinate.
     * @param y Tile Y coordinate.
     * @param layer Layer index (0-based, 0 to layer_count-1).
     * @param cameraPos Camera position in world coordinates.
     */
    void RenderSingleTile(IRenderer& r, int x, int y, int layer, glm::vec2 cameraPos);

    /** @} */

    /**
     * @name Serialization functions
     * @brief Map persistence using JSON format.
     * @{
     */
    /**
     * @brief Save map to JSON file.
     *
     * Saves all editor-authored map surfaces in a compact sparse format:
     * dimensions, layer metadata and per-tile fields, collision, navigation,
     * elevation, corner-cut masks, no-projection structures, particle zones,
     * world lights, animated tile definitions/placements, NPCs/dialogue, and
     * optional player spawn state.
     *
     * @par JSON structure
     * Top-level fields are intentionally sparse; per-cell objects use the
     * row-major flat index `i = y * width + x`.
     * @code{.json}
     * {
     *   "width": 64,
     *   "height": 64,
     *   "tileWidth": 16,
     *   "tileHeight": 16,
     *   "collision": [42, 43],
     *   "navigation": [100, 101],
     *   "elevation": { "512": 8 },
     *   "dynamicLayers": [
     *     {
     *       "name": "Ground",
     *       "renderOrder": 0,
     *       "isBackground": true,
     *       "tiles": { "42": 15 },
     *       "rotation": { "42": 90.0 },
     *       "stance": { "42": 3 },
     *       "elevationRole": { "42": 1 },
     *       "flipX": [42],
     *       "flipY": [],
     *       "ySortPlus": [],
     *       "ySortMinus": [],
     *       "structureId": { "42": 0 }
     *     }
     *   ],
     *   "noProjectionStructures": [
     *     { "id": 0, "name": "Cabin", "leftAnchor": [160, 192], "rightAnchor": [208, 192] }
     *   ],
     *   "particleZones": [
     *     { "x": 10, "y": 20, "width": 64, "height": 32, "type": 0,
     *       "enabled": true, "noProjection": false }
     *   ],
     *   "worldLights": [
     *     { "x": 120, "y": 88, "r": 1.0, "g": 0.85, "b": 0.55, "radius": 64,
     *       "schedule": "NightOnly" }
     *   ],
     *   "animatedTiles": [{ "frames": [1, 2, 3], "frameDuration": 0.2 }],
     *   "layerAnimationMaps": [{ "42": 0 }],
     *   "cornerCutBlocked": { "42": 3 },
     *   "npcs": [
     *     { "type": "BW2_NPC1", "tileX": 10, "tileY": 5, "name": "Ari",
     *       "dialogueTree": { "...": "..." } }
     *   ],
     *   "player": { "tileX": 5, "tileY": 5, "characterType": 0 }
     * }
     * @endcode
     *
     * @htmlonly
     * <pre class="mermaid">
     * flowchart LR
     *     MapJSON["Map JSON"] --> Layers["dynamicLayers[]"]
     *     MapJSON --> Grids["collision / navigation / elevation / cornerCutBlocked"]
     *     MapJSON --> Structures["noProjectionStructures[]"]
     *     MapJSON --> Effects["particleZones[] / worldLights[] / animatedTiles[]"]
     *     MapJSON --> Actors["npcs[] / player"]
     *     Layers --> PerTile["tiles, rotation, stance, elevationRole, flips, y-sort, structureId,
     * animationMap"] Structures --> PerTile
     * </pre>
     * @endhtmlonly
     *
     * @param filename Output JSON file path.
     * @param npcs Optional NPC list to save.
     * @param playerTileX Player tile X (-1 to skip).
     * @param playerTileY Player tile Y (-1 to skip).
     * @param characterType Player's character type (-1 to skip).
     * @return `true` if saved successfully.
     */
    bool SaveMapToJSON(const std::string& filename,
                       const ecs::registry* npcs = nullptr,
                       int playerTileX = -1,
                       int playerTileY = -1,
                       int characterType = -1) const;

    /**
     * @brief Load map from JSON file.
     *
     * Loads map dimensions, layers, collision/navigation, elevation, and optional
     * NPC/player data from JSON, replacing all current map state. Returns `false`
     * on file-open or parse failure.
     *
     * Tolerant of legacy key names: "ySorted", "navmesh" and a flat "animationMap"
     * are still accepted.
     *
     * Per-entry tolerance is not universal. It covers the per-cell grids and the
     * `dynamicLayers` fields, where a malformed or out-of-range entry is skipped
     * (with a capped warning count) and the rest of the load continues. The
     * `cornerCutBlocked`, `particleZones`, `worldLights`, `noProjectionStructures`
     * and `animatedTiles` sections parse without a per-entry guard, as do the
     * values in `layerAnimationMaps`, so one malformed entry there throws to the
     * outer handler: the load returns `false` and the tilemap is reset to a clean
     * empty state rather than left half-populated.
     *
     * @par Stance migration
     * A layer with no `stance` key predates @ref TileStance and is migrated once, at
     * load: a `noProjection` cell becomes TileStance::Structure; a y-sort cell on an
     * object or foreground layer becomes TileStance::Wall when a 4-connected neighbour
     * also migrates upright and TileStance::Prop when it stands alone; everything else
     * stays TileStance::Flat. This is the only place the layer index or a cell's
     * neighbours may influence a stance - see the note in TileRole.hpp. Re-saving the
     * map writes `stance` and drops `noProjection`, which older builds cannot read.
     *
     * @param filename Input JSON file path.
     * @param npcs Registry to repopulate. When the file carries an `npcs` array,
     *             every existing NPC entity in this registry is destroyed before the
     *             new roster spawns; a file without that key leaves the registry
     *             untouched. Pass nullptr to skip NPC loading entirely.
     * @param playerTileX Optional output for player X coordinate.
     * @param playerTileY Optional output for player Y coordinate.
     * @param characterType Optional output for player's character type.
     * @return `true` if loaded successfully.
     */
    bool LoadMapFromJSON(const std::string& filename,
                         ecs::registry* npcs = nullptr,
                         int* playerTileX = nullptr,
                         int* playerTileY = nullptr,
                         int* characterType = nullptr);
    /** @} */

    /**
     * @name Tileset utilities
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
     * @name Particle zones
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

    /**
     * @brief Insert a particle zone at a specific index, shifting existing
     * zones at idx and beyond by one.
     *
     * Used by the editor's RemoveParticleZoneCmd::Revert to restore a zone
     * to its original index (preserves index-based ParticleSystem tracking).
     */
    void InsertParticleZoneAt(size_t index, const ParticleZone& zone)
    {
        if (index <= m_ParticleZones.size())
            m_ParticleZones.insert(m_ParticleZones.begin() + index, zone);
    }

    /** @} */

    /** @name World lights
     * @brief Persistent point-light sources anchored to world positions.
     *
     * Rendered as additive soft-circle sprites in `Game::Render` with intensity
     * driven by `ComputeLightIntensity(schedule, hour)`. Serialized in the map
     * JSON's `worldLights` array.
     * @{
     */

    /// @brief Read-only access to all world lights.
    const std::vector<WorldLight>& GetLights() const { return m_Lights; }

    /// @brief Mutable access to all world lights.
    std::vector<WorldLight>& GetLightsMutable() { return m_Lights; }

    /// @brief Append a light to the registry.
    void AddLight(const WorldLight& light) { m_Lights.push_back(light); }

    /// @brief Remove a light by index. Returns true on success.
    bool RemoveLight(size_t index)
    {
        if (index >= m_Lights.size())
            return false;
        m_Lights.erase(m_Lights.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    /// @brief Remove all lights.
    void ClearLights() { m_Lights.clear(); }

    /** @} */

    /** @name Animated tiles
     * @brief Methods for managing animated tile definitions.
     * @{
     */
    /**
     * @brief Pop the most recently added animation. Used by AddAnimatedTileCmd::Revert
     * under the strict LIFO invariant (no per-tile animationMap reference survives
     * because the corresponding SetTileAnimationCmd::Revert ran first).
     */
    void PopLastAnimatedTile()
    {
        if (!m_AnimatedTiles.empty())
            m_AnimatedTiles.pop_back();
    }

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
     *
     * When @p animId names an animation that has at least one frame, this also
     * writes that animation's first frame into the layer's tile id at
     * (@p x, @p y), overwriting whatever was painted there. Clearing with -1
     * leaves the last written tile id in place, so an editor command must save
     * and restore the original tile id as well as the animation id to undo
     * losslessly.
     *
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
        size_t idx = FlatIndex(x, y);
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
        size_t idx = FlatIndex(x, y);
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
     * @name Layer field accessor templates
     * @brief Generic get/set for any per-tile defaulted_vector field on TileLayer.
     * @{
     */

    /// Get a per-tile field value with bounds checking. Returns the field's default on OOB.
    template <auto Field>
    auto GetLayerField(int x, int y, size_t layer) const
    {
        using Vec = std::decay_t<decltype(std::declval<TileLayer>().*Field)>;
        if (layer >= m_Layers.size() || x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
            return static_cast<typename Vec::value_type>(Vec::default_value);
        return static_cast<typename Vec::value_type>((m_Layers[layer].*Field)[FlatIndex(x, y)]);
    }

    /// Set a per-tile field value with bounds checking. Silently ignores OOB.
    template <auto Field>
    void SetLayerField(
        int x,
        int y,
        size_t layer,
        typename std::decay_t<decltype(std::declval<TileLayer>().*Field)>::value_type value)
    {
        if (layer >= m_Layers.size() || x < 0 || x >= m_MapWidth || y < 0 || y >= m_MapHeight)
            return;
        (m_Layers[layer].*Field)[FlatIndex(x, y)] = value;
    }

    /// @}

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
     * @name Array access operators
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
    ColumnProxy<defaulted_vector<int, -1>, int, -1> operator[](int x)
    {
        return ColumnProxy<defaulted_vector<int, -1>, int, -1>(
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
    ConstColumnProxy<defaulted_vector<int, -1>, int, -1> operator[](int x) const
    {
        return ConstColumnProxy<defaulted_vector<int, -1>, int, -1>(
            &m_Layers[0].tiles, &m_MapWidth, &m_MapHeight, x);
    }
    /** @} */

private:
    /**
     * @name Tileset
     * @{
     */
    Texture m_TilesetTexture;               ///< Combined tileset texture.
    int m_TileWidth{16}, m_TileHeight{16};  ///< Tile dimensions in pixels.
    /// Combined atlas dimensions in pixels, not tile counts. The height grows
    /// when @ref PackAdditionalSheets appends character sheets.
    int m_TilesetWidth{0}, m_TilesetHeight{0};
    int m_TilesPerRow{0};  ///< Tiles per row in tileset.
    /// @brief Smart pointer with custom deleter for tileset pixel data.
    using TilesetDataPtr = std::unique_ptr<unsigned char[], void (*)(unsigned char*)>;
    TilesetDataPtr m_TilesetData{nullptr, +[](unsigned char* p) { delete[] p; }};
    int m_TilesetDataWidth{0}, m_TilesetDataHeight{0};  ///< Raw image dimensions.
    int m_TilesetChannels{0};  ///< Number of color channels (3=RGB, 4=RGBA).
    /**
     * Height of the atlas before any character sprite sheets were packed.
     * PackAdditionalSheets uses this as the baseline so subsequent calls
     * replace (not stack on top of) any previously-packed characters.
     */
    int m_TilesetOnlyHeight{0};
    std::vector<uint8_t> m_TileTransparencyCache;  ///< Cached transparency results per tile ID.
    bool m_TransparencyCacheBuilt{false};          ///< Whether the cache has been built.

    /**
     * Pixel offsets within the atlas for character sprite sheets packed via
     * @ref PackAdditionalSheets. Keyed by caller-supplied identifier.
     */
    std::unordered_map<std::string, glm::vec2> m_CharacterAtlasOffsets;
    /// @}

    /**
     * @name Map dimensions
     * @{
     */
    int m_MapWidth{125}, m_MapHeight{125};  ///< Map dimensions in tiles.
    /// @}

    /**
     * @name Dynamic layers
     * @{
     */
    /**
     * @brief All tile layers, in index order.
     *
     * Size is data-driven: the default stack is 10 (5 background + 5 foreground), but
     * LoadMapFromJSON rebuilds this from the map's dynamicLayers[] array. Read it through
     * GetLayerCount(), never as a constant.
     */
    std::vector<TileLayer> m_Layers;
    /// @}

    /**
     * @name Collision and navigation
     * @{
     */
    CollisionMap<std::vector> m_CollisionMap;    ///< Collision flags.
    NavigationMap<std::vector> m_NavigationMap;  ///< NPC walkability flags.
    std::vector<uint8_t>
        m_CornerCutBlocked;  ///< Per-tile corner cut disable mask (4 bits per tile).
    /// @}

    /**
     * @name Elevation data
     * @{
     */
    std::vector<int> m_Elevation;  ///< Per-tile elevation in pixels (0 = ground).
    mutable std::vector<int>
        m_ElevationRegionIds;  ///< Cached connected-component id per elevation cell.
    mutable bool m_ElevationRegionIdsDirty{true};

    /// @brief Rebuild connected non-zero-elevation component IDs after map edits.
    void RebuildElevationRegionIds() const;
    /// @}

    /**
     * @name Particle zones
     * @{
     */
    std::vector<ParticleZone> m_ParticleZones;  ///< Placeable particle emitter zones.
    /// @}

    /**
     * @name World lights
     * @{
     */
    std::vector<WorldLight> m_Lights;  ///< Persistent point lights (lamps, windows).
    /// @}

    /**
     * @name Animated tiles
     * @{
     */
    std::vector<AnimatedTile> m_AnimatedTiles;  ///< Animation definitions.
    /// Write-only leftover: sized/cleared by the constructor and SetTilemapSize and never
    /// read, saved, or loaded. The live per-tile animation ids are TileLayer::animationMap.
    std::vector<int> m_TileAnimationMap;
    float m_AnimationTime{0.0f};  ///< Global animation timer.
    /// @}

    /**
     * @name No-projection structures
     * @{
     */
    std::vector<NoProjectionStructure>
        m_NoProjectionStructures;  ///< Manually defined structures with anchors.
    /// @}

    /**
     * @name Structure bounds cache
     * @brief Cached bounding boxes per (layerIndex, structureId) to avoid full-map scans.
     * @{
     */

    /// Cached bounding box for a structure in a specific layer.
    struct StructureBounds
    {
        int minX, maxX, minY, maxY;
    };

    /// Rebuild the full structure bounds cache from tile data (called lazily).
    void RebuildStructureBoundsCache() const;

    /// Rebuild bounds for a single (layer, structId) pair by scanning one layer.
    void RebuildSingleStructureBounds(size_t layerIdx, int structId, int64_t key) const;

    /// Invalidate the entire cache so it is rebuilt on next access.
    void InvalidateStructureBoundsCache();

    /**
     * Incrementally update cache when a single tile's structure ID changes.
     * Expands new-structure bounds O(1); marks old structure dirty for lazy re-scan.
     */
    void InvalidateStructureBoundsForTile(
        size_t layerIdx, int x, int y, int oldStructId, int newStructId);

    /**
     * Look up cached bounds for a (layer, structId) pair.
     * @return Pointer to bounds, or nullptr if not found.
     */
    const StructureBounds* GetCachedStructureBounds(size_t layerIdx, int structId) const;

    mutable std::unordered_map<int64_t, StructureBounds>
        m_StructureBoundsCache;  ///< (layerIdx<<32 | structId) -> bounds.
    mutable std::unordered_set<int64_t> m_DirtyStructureKeys;  ///< Per-structure dirty keys.
    mutable bool m_StructureBoundsCacheDirty = true;  ///< Whether the full cache needs a rebuild.
    /// @}

    /**
     * @name Render cache (reused each frame to avoid allocations)
     * @{
     */
    mutable std::vector<DepthSortedTile>
        m_DepthSortedTilesCache;                 ///< Cached depth tiles (reused each frame).
    mutable std::vector<bool> m_ProcessedCache;  ///< Cached processed flags (reused each frame).
    mutable std::vector<bool>
        m_RenderedStructuresCache;                   ///< Cached structure flags, reused each frame.
    mutable std::vector<bool> m_FloodFillProcessed;  ///< Reusable buffer for flood-fill searches.
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
