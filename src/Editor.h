#pragma once

#include "IRenderer.h"
#include "NonPlayerCharacter.h"
#include "ParticleSystem.h"
#include "PlayerCharacter.h"
#include "Tilemap.h"

#include <GLFW/glfw3.h>
#include <bitset>
#include <glm/glm.hpp>
#include <string>
#include <vector>

/**
 * @struct EditorContext
 * @brief Lightweight bridge giving the Editor read/write access to Game-owned state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * EditorContext is constructed by Game::MakeEditorContext() each frame and passed
 * by value to every Editor method. Value members are snapshots (window size,
 * visible tiles); reference members allow the editor to mutate shared state
 * (camera position, zoom, free-camera flag) without a back-pointer to Game.
 *
 * @par Usage
 * @code{.cpp}
 * // Inside Game:
 * EditorContext ctx = MakeEditorContext();
 * m_Editor.ProcessInput(deltaTime, ctx);
 * m_Editor.Render(ctx);
 * @endcode
 *
 * @par Design Rationale
 * Using a context struct instead of a Game pointer keeps Editor decoupled from
 * the Game class definition. Editor.h never includes Game.h, which prevents
 * circular dependencies and makes the editor testable in isolation.
 *
 * @see Editor, Game::MakeEditorContext()
 *
 * @warning **Do not store.** This struct is designed for single-frame,
 * pass-by-reference use only. Reference members will dangle if the
 * context outlives the frame in which it was created.
 */
struct EditorContext
{
    GLFWwindow* window;                     ///< GLFW window handle for input queries.
    int screenWidth;                        ///< Window width in pixels.
    int screenHeight;                       ///< Window height in pixels.
    int tilesVisibleWidth;                  ///< Tiles visible horizontally at current zoom.
    int tilesVisibleHeight;                 ///< Tiles visible vertically at current zoom.
    glm::vec2& cameraPosition;              ///< Current camera world position (mutable).
    glm::vec2& cameraFollowTarget;          ///< Camera follow target position (mutable).
    bool& hasCameraFollowTarget;            ///< Whether camera has a follow target (mutable).
    float& cameraZoom;                      ///< Camera zoom level (mutable).
    bool& freeCameraMode;                   ///< Free camera toggle (mutable).
    bool& enable3DEffect;                   ///< 3D perspective toggle (mutable).
    float& cameraTilt;                      ///< Camera tilt angle (mutable).
    float& globeSphereRadius;               ///< Globe effect radius (mutable).
    Tilemap& tilemap;                       ///< Active tilemap for tile queries and edits.
    PlayerCharacter& player;                ///< Player character for position/rendering.
    std::vector<NonPlayerCharacter>& npcs;  ///< NPC list for placement and debug display.
    IRenderer& renderer;                    ///< Active renderer for drawing overlays.
    ParticleSystem& particles;              ///< Particle system for zone editing.
};

/**
 * @class Editor
 * @brief Level editor with tile placement, overlay rendering, and debug tools.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Editor owns all editor-specific state (mode flags, tile selection, mouse
 * tracking, tile picker camera) and implements tile placement, overlay
 * rendering, and debug visualization. Game delegates to Editor via an
 * EditorContext built each frame; Editor never holds a pointer to Game.
 *
 * @par Activation
 * Toggled with the **E** key in Game::ProcessInput(). When active the tile
 * picker opens automatically; when deactivated it closes.
 *
 * @par Editor Modes
 * Only one sub-mode is active at a time, selected by hotkey:
 *
 * | Key | Mode               | Left-Click Action                | Right-Click Action           |
 * |-----|--------------------|----------------------------------|------------------------------|
 * |   T | Tile Picker        | Select tile / multi-tile region  | -                            |
 * |   M | Navigation Edit    | -                                | Toggle walkability (drag)    |
 * |   N | NPC Placement      | Place / remove NPC               | -                            |
 * |   B | No-Projection Edit | Set no-projection flag (flood)   | Clear flag (flood)           |
 * |   G | Structure Edit     | Anchor + flood assign structure  | Clear structure assignment   |
 * |   H | Elevation Edit     | Paint elevation value            | Clear elevation              |
 * |   J | Particle Zone Edit | Drag to create zone              | Remove zone                  |
 * |   K | Animation Edit     | Apply animation to tile          | Remove animation             |
 * |   Y | Y-Sort-Plus Edit   | Set Y-sort-plus flag             | Clear Y-sort-plus flag       |
 * |   O | Y-Sort-Minus Edit  | Set Y-sort-minus flag            | Clear Y-sort-minus flag      |
 * |   - | Default            | Place selected tile (drag)       | Toggle collision (drag)      |
 *
 * @par Per-Frame Pipeline
 * @code
 * Game::ProcessInput   -->  Editor::ProcessInput       (keyboard)
 *                      -->  Editor::ProcessMouseInput  (mouse)
 * Game::Update         -->  Editor::Update          (tile picker smoothing)
 * Game::Render         -->  Editor::Render          (overlays + tile picker)
 * Game::ScrollCallback -->  Editor::HandleScroll    (elevation / tile picker)
 * @endcode
 *
 * @par Debug Overlays (F3)
 * When debug mode is active (toggled independently of editor mode), all
 * overlay layers are rendered: collision, navigation, elevation, corner
 * cutting, no-projection, structures, Y-sort flags, particle zones, and
 * NPC patrol info.
 *
 * @see EditorContext, Game::MakeEditorContext()
 */
class Editor
{
public:
    /// @brief Construct editor with all modes disabled.
    Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = default;
    Editor& operator=(Editor&&) = default;

    /**
     * @brief Initialize editor with available NPC types.
     * @param npcTypes List of NPC sprite paths.
     */
    void Initialize(const std::vector<std::string>& npcTypes);

    /// @brief Check if the level editor is active.
    /// @return `true` when the editor is active (toggled with E key).
    [[nodiscard]] bool IsActive() const { return m_Active; }

    /// @brief Push an on-screen status toast (e.g. from save/load).
    /// @param message Text to display.
    /// @param color Tint for the text (green for success, red for error).
    /// @param durationSeconds How long the toast stays on screen.
    void ShowStatus(std::string message, glm::vec3 color, float durationSeconds = 3.0f);

    /// @brief Activate or deactivate the level editor.
    /// @param active `true` to enable, `false` to disable.
    void SetActive(bool active);

    /**
     * @brief Process keyboard input for editor hotkeys and mode switching.
     * @param deltaTime Frame time in seconds (for camera pan speed).
     * @param ctx Editor context providing window and game state.
     */
    void ProcessInput(float deltaTime, const EditorContext& ctx);

    /**
     * @brief Process mouse input for tile placement and drag operations.
     * @param ctx Editor context providing cursor position and game state.
     */
    void ProcessMouseInput(const EditorContext& ctx);

    /**
     * @brief Handle scroll wheel input for elevation and tile picker zoom.
     * @param yoffset Scroll delta (positive = scroll up).
     * @param ctx Editor context providing current mode state.
     */
    void HandleScroll(double yoffset, const EditorContext& ctx);

    /**
     * @brief Update editor state (tile picker smooth scrolling).
     * @param deltaTime Frame time in seconds.
     * @param ctx Editor context providing game state.
     */
    void Update(float deltaTime, const EditorContext& ctx);

    /**
     * @brief Render editor overlays and tile picker.
     *
     * Handles perspective suspension internally for the tile picker.
     * Called from Game::Render() when editor or debug mode is active.
     */
    void Render(const EditorContext& ctx);

    /**
     * @brief Render no-projection anchor markers on top of everything.
     *
     * Separate from Render() because anchors must appear above all UI.
     * Caller is responsible for suspending perspective before calling.
     */
    void RenderNoProjectionAnchors(const EditorContext& ctx);

    /// @brief Check if debug overlays are enabled.
    /// @return `true` when debug overlays are visible (F3 toggle).
    [[nodiscard]] bool IsDebugMode() const { return m_DebugMode; }

    /// @brief Check if text debug info (FPS, coords) is shown.
    /// @return `true` when debug text is visible.
    [[nodiscard]] bool IsShowDebugInfo() const { return m_ShowDebugInfo; }

    /// @brief Check if no-projection anchor markers are shown.
    /// @return `true` when anchor markers are visible.
    [[nodiscard]] bool IsShowNoProjectionAnchors() const { return m_ShowNoProjectionAnchors; }

    /// @brief Check if the tile picker panel is visible.
    /// @return `true` when the tile picker is open.
    [[nodiscard]] bool IsShowTilePicker() const { return m_ShowTilePicker; }

    /// @brief Toggle debug overlay rendering on/off.
    void ToggleDebugMode();

    /// @brief Toggle text debug info display on/off.
    void ToggleShowDebugInfo();

    /**
     * @brief Reset tile picker zoom and pan to defaults.
     *
     * Called from Game when Z key is pressed in editor mode.
     */
    void ResetTilePickerState();

private:
    /// @name Render Methods
    /// @brief Private overlay and UI rendering routines.
    /// @{

    /// @brief Render editor UI elements (cursor, tile info, status text).
    void RenderEditorUI(const EditorContext& ctx);
    /// @brief Render collision flag overlay.
    void RenderCollisionOverlays(const EditorContext& ctx);
    /// @brief Render navigation walkability overlay.
    void RenderNavigationOverlays(const EditorContext& ctx);
    /// @brief Render elevation value overlay.
    void RenderElevationOverlays(const EditorContext& ctx);
    /// @brief Render no-projection flag overlay.
    void RenderNoProjectionOverlays(const EditorContext& ctx);
    /// @brief Render no-projection anchor markers (implementation).
    void RenderNoProjectionAnchorsImpl(const EditorContext& ctx);
    /// @brief Render structure assignment overlay.
    void RenderStructureOverlays(const EditorContext& ctx);
    /// @brief Render generic layer flag overlay using a getter method pointer.
    void RenderLayerFlagOverlays(const EditorContext& ctx,
                                 bool editMode,
                                 bool (Tilemap::*getter)(int, int, size_t) const,
                                 const glm::vec3& color);
    /// @brief Render Y-sort-plus flag overlay.
    void RenderYSortPlusOverlays(const EditorContext& ctx);
    /// @brief Render Y-sort-minus flag overlay.
    void RenderYSortMinusOverlays(const EditorContext& ctx);
    /// @brief Render particle zone boundary overlay.
    void RenderParticleZoneOverlays(const EditorContext& ctx);
    /// @brief Render NPC debug info (patrol routes, waypoints).
    void RenderNPCDebugInfo(const EditorContext& ctx);
    /// @brief Render corner-cutting debug overlay.
    void RenderCornerCuttingOverlays(const EditorContext& ctx);
    /// @brief Render color-coded layer indicator overlay.
    void RenderLayerOverlay(const EditorContext& ctx, int layerIndex, const glm::vec4& color);
    /// @brief Render tile placement preview at cursor position.
    void RenderPlacementPreview(const EditorContext& ctx);

    /// @}

    /// @brief Clear all mutually exclusive edit sub-modes.
    void ClearAllEditModes();

    /// @brief Recalculate all NPC patrol routes after navigation changes.
    void RecalculateNPCPatrolRoutes(const EditorContext& ctx);

    /// @brief Lazily compute and cache no-projection structure bounds for the current frame.
    void EnsureNoProjBoundsCache(const EditorContext& ctx);

    /// @brief Map rotated tile offset to source tile coordinates.
    /// @param dx Rotated offset X.
    /// @param dy Rotated offset Y.
    /// @param sourceDx Output source offset X.
    /// @param sourceDy Output source offset Y.
    void CalculateRotatedSourceTile(int dx, int dy, int& sourceDx, int& sourceDy) const;

    /// @brief Get tile rotation compensated for current editor rotation.
    float GetCompensatedTileRotation() const;

    /// @brief Toggle a per-tile layer flag using the provided setter.
    /// @param ctx Editor context.
    /// @param tileX Tile column.
    /// @param tileY Tile row.
    /// @param setter Tilemap method pointer for setting the flag.
    /// @param flagName Name of the flag (for debug display).
    void SetLayerFlagAtTile(const EditorContext& ctx,
                            int tileX,
                            int tileY,
                            void (Tilemap::*setter)(int, int, size_t, bool),
                            const std::string& flagName);

    /**
     * @struct TileZoneRect
     * @brief Axis-aligned rectangle in tile coordinates for zone editing.
     */
    struct TileZoneRect
    {
        float x, y, w, h;  ///< Position and size in world pixels.
    };

    /// @brief Calculate particle zone rectangle from world position.
    TileZoneRect CalculateParticleZoneRect(float worldX,
                                           float worldY,
                                           int tileWidth,
                                           int tileHeight) const;

    /**
     * @enum EditMode
     * @brief Mutually exclusive editor sub-modes, selected by hotkey.
     *
     * Only one sub-mode is active at a time. `None` means the default
     * tile-placement mode is active (left-click places, right-click toggles collision).
     */
    enum class EditMode
    {
        None,          ///< Default tile placement / collision toggle.
        Navigation,    ///< Painting walkability flags (M key).
        Elevation,     ///< Painting elevation values (H key).
        NPCPlacement,  ///< Placing / removing NPCs (N key).
        NoProjection,  ///< Editing no-projection flags (B key).
        YSortPlus,     ///< Editing Y-sort-plus flags (Y key).
        YSortMinus,    ///< Editing Y-sort-minus flags (O key).
        ParticleZone,  ///< Defining particle emitter zones (J key).
        Structure,     ///< Assigning tiles to structures (G key).
        Animation,     ///< Applying animations to tiles (K key).
    };

    /// @name Mode State
    /// @{
    bool m_Active;          ///< Master toggle for the level editor (E key).
    bool m_ShowTilePicker;  ///< Whether the tile picker panel is visible.
    EditMode m_EditMode;    ///< Current sub-mode (only one active at a time).
    /// @}

    /// @name Particle Zone Editing
    /// State for drag-to-create particle emitter zones.
    /// @{
    ParticleType m_CurrentParticleType;  ///< Visual type for new zones (e.g. Firefly).
    bool m_ParticleNoProjection;         ///< If true, new zones skip perspective projection.
    bool m_PlacingParticleZone;          ///< True while the user is dragging to define a zone.
    glm::vec2 m_ParticleZoneStart;       ///< World position where the current drag began.
    /// @}

    /// @name Structure Editing
    /// State for the two-anchor structure workflow: place left anchor, right anchor,
    /// then flood-assign tiles between them to a structure ID.
    /// @{
    int m_CurrentStructureId;          ///< Active structure ID, or -1 if none selected.
    int m_PlacingAnchor;               ///< Anchor step: 0 = idle, 1 = left, 2 = right.
    glm::vec2 m_TempLeftAnchor;        ///< World position of left anchor (-1 = unset).
    glm::vec2 m_TempRightAnchor;       ///< World position of right anchor (-1 = unset).
    bool m_AssigningTilesToStructure;  ///< True during tile flood-assign phase.
    /// @}

    /// @name Animation Editing
    /// @{
    std::vector<int> m_AnimationFrames;  ///< Tile IDs composing the current animation sequence.
    float m_AnimationFrameDuration;      ///< Seconds each frame is shown (default 0.2s).
    int m_SelectedAnimationId;           ///< Index of the animation being edited, or -1.
    /// @}

    /// @name Debug Flags
    /// @{
    bool m_DebugMode;                ///< Enables all debug overlays (F3).
    bool m_ShowDebugInfo;            ///< Shows text debug info (FPS, tile coords, etc.).
    bool m_ShowNoProjectionAnchors;  ///< Renders no-projection anchor markers on top of UI.
    /// @}

    /// @name Status toast
    /// On-screen transient message (save success/failure, load result). Drawn
    /// while m_StatusTimer > 0 and decremented by Update().
    /// @{
    std::string m_StatusMessage;       ///< Text to display; empty = hidden.
    float m_StatusTimer = 0.0f;        ///< Seconds remaining to display.
    glm::vec3 m_StatusColor{1, 1, 1};  ///< Tint (e.g. green for success, red for error).
    /// @}

    /// @name Tile Selection
    /// Currently selected tile, layer, and elevation for placement.
    /// @{
    int m_SelectedTileID;    ///< Tile atlas index chosen in the tile picker.
    int m_CurrentLayer;      ///< Active tilemap layer (0-9) for placement.
    int m_CurrentElevation;  ///< Active elevation level (default 4 = ground).
    /// @}

    /// @name NPC Types
    /// @{
    std::vector<std::string> m_AvailableNPCTypes;  ///< Sprite paths loaded at init.
    size_t m_SelectedNPCTypeIndex;                 ///< Index into m_AvailableNPCTypes.

    /// @brief Clamp m_SelectedNPCTypeIndex to valid range after vector changes.
    void ClampNPCTypeIndex()
    {
        if (!m_AvailableNPCTypes.empty())
            m_SelectedNPCTypeIndex =
                std::min(m_SelectedNPCTypeIndex, m_AvailableNPCTypes.size() - 1);
    }
    /// @}

    /// @name Mouse/Drag State
    /// Tracks mouse position and per-mode drag state. "Last" tile coords use -1
    /// as a sentinel meaning "no tile touched yet this drag".
    /// @{
    struct MouseDragState
    {
        double lastMouseX = 0.0;
        double lastMouseY = 0.0;
        bool mousePressed = false;
        bool rightMousePressed = false;
        int lastPlacedTileX = -1;
        int lastPlacedTileY = -1;
        int lastNavigationTileX = -1;
        int lastNavigationTileY = -1;
        bool navigationDragState = false;
        int lastCollisionTileX = -1;
        int lastCollisionTileY = -1;
        bool collisionDragState = false;
        int lastNPCPlacementTileX = -1;
        int lastNPCPlacementTileY = -1;
    };
    MouseDragState m_Mouse;
    /// @}

    /// @name Tile Picker State
    /// Camera controls for the tile picker panel (zoom + smooth-scrolled offset).
    /// @{
    struct TilePickerCamera
    {
        float zoom = 2.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float targetOffsetX = 0.0f;
        float targetOffsetY = 0.0f;
    };
    TilePickerCamera m_TilePicker;
    /// @}

    /// @name Multi-Tile Selection
    /// Allows selecting and placing rectangular regions of tiles from the picker.
    /// @{
    struct MultiTileState
    {
        bool selectionMode = false;
        int selectedStartID = 0;
        int width = 1;
        int height = 1;
        bool isSelecting = false;
        int selectionStartTileID = -1;
        float placementCameraZoom = 1.0f;
        bool isPlacing = false;
        int rotation = 0;
    };
    MultiTileState m_MultiTile;
    /// @}

    /// @name Key Debounce State
    /// Per-key pressed tracking for edge-triggered input (replaces function-local statics).
    /// @{
    std::bitset<GLFW_KEY_LAST + 1> m_KeyPressed;  ///< True while a key is held from last press.
    int m_LastDeletedTileX;                       ///< Last tile column erased during delete-drag.
    int m_LastDeletedTileY;                       ///< Last tile row erased during delete-drag.
    /// @}

    /// @name No-Projection Bounds Cache
    /// Computed once per frame and shared between overlay rendering passes.
    /// @{
    struct NoProjGroupBounds
    {
        int minX, maxX, minY, maxY;
    };
    std::vector<NoProjGroupBounds> m_CachedNoProjBounds;
    bool m_NoProjBoundsCached = false;  ///< Reset at the start of each Render() call.
    /// @}
};
