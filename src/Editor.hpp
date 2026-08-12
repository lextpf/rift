#pragma once

#include "EditorCommands.hpp"
#include "EditorStrokeAccumulators.hpp"
#include "IRenderer.hpp"
#include "ParticleSystem.hpp"
#include "Tilemap.hpp"
#include "UndoRedoStack.hpp"

#include <GLFW/glfw3.h>
#include <bitset>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct CameraState;

/**
 * @struct EditorContext
 * @brief Lightweight bridge giving the Editor read/write access to Game-owned state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * EditorContext is constructed by Game::MakeEditorContext() each frame and passed
 * as `const EditorContext&` to every Editor method. Value members are snapshots
 * (window size, visible tiles); reference members allow the editor to mutate shared
 * state (camera position, zoom, free-camera flag) without a back-pointer to Game -
 * const on the context does not make the referenced Game state const.
 *
 * @par Usage
 * @code{.cpp}
 * // Inside Game:
 * EditorContext ctx = MakeEditorContext();
 * m_Editor.ProcessInput(deltaTime, ctx);
 * m_Editor.Render(ctx);
 * @endcode
 *
 * @par Design rationale
 * Using a context struct instead of a Game pointer keeps Editor decoupled from
 * the Game class definition. Editor.hpp never includes Game.hpp, which prevents
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
    GLFWwindow* window;         ///< GLFW window handle for input queries.
    int screenWidth;            ///< Window width in pixels.
    int screenHeight;           ///< Window height in pixels.
    int tilesVisibleWidth;      ///< Tiles visible horizontally at current zoom.
    int tilesVisibleHeight;     ///< Tiles visible vertically at current zoom.
    CameraState& camera;        ///< Camera state (position, follow, zoom, free mode), mutable.
    Tilemap& tilemap;           ///< Active tilemap for tile queries and edits.
    ecs::entity playerEntity;   ///< Player entity (resolve via @ref npcs, the world).
    ecs::registry& npcs;        ///< Live NPC + player store (the ECS registry / world).
    IRenderer& renderer;        ///< Active renderer for drawing overlays.
    ParticleSystem& particles;  ///< Particle system for zone editing.
    std::string saveMapPath;    ///< Save/load JSON path configured by project manifest.
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
 * Toggled via the developer-console `editor [on|off|toggle]` command. When
 * active the tile picker opens automatically; when deactivated it closes.
 *
 * @par Editor modes
 * Only one sub-mode is active at a time, selected by hotkey:
 *
 * | Key | Mode               | Left-Click Action                | Right-Click Action           |
 * |-----|--------------------|----------------------------------|------------------------------|
 * |   T | Tile Picker        | Select tile / multi-tile region  | - (picker swallows it)       |
 * |   M | Navigation Edit    | (falls through to Default)       | Toggle walkability (drag)    |
 * |   N | NPC Placement      | Place / remove NPC               | (falls through to Default)   |
 * |   B | Stance Edit        | Paint selected stance            | Reset to Flat                |
 * |   G | Structure Edit     | Anchor / flood / stance toggle   | Clear structure assignment   |
 * |   H | Elevation Edit     | Paint height + role              | Clear height + role          |
 * |   J | Particle Zone Edit | Drag to create zone              | Remove zone                  |
 * |   K | Animation Edit     | Apply animation to tile          | Remove animation             |
 * |   Y | Y-Sort-Plus Edit   | Set Y-sort-plus flag             | Clear Y-sort-plus flag       |
 * |   O | Y-Sort-Minus Edit  | Set Y-sort-minus flag            | Clear Y-sort-minus flag      |
 * |   - | Default            | Place selected tile (drag)       | Toggle collision (drag)      |
 *
 * A mode claims only the buttons listed for it. Each button is dispatched as one ordered
 * if/else chain, so a mode that does not claim a button falls through to the Default row's
 * action for that button: a left click in M paints a tile, a right click in N paints
 * collision. Holding Shift widens the single-cell edit of B, G, Y and O to a 4-connected
 * flood fill. In G, Ctrl+click sets the left anchor and a second Ctrl+click sets the right
 * anchor and creates the structure, Shift+click flood-assigns the current structure ID, and
 * a plain click toggles the tile between the Structure and Flat stance.
 *
 * @verbatim
 * LEFT    Ctrl-drag map selection (every mode but G)  -- consumes the button
 *         tile picker open                           -- consumes the button
 *         N -> J -> K (only with a selected animation) -> H -> G -> B -> Y -> O
 *         fall-through: stamp the multi-tile brush, else paint one tile
 *
 * RIGHT   tile picker open                           -- suppressed entirely
 *         K -> H -> G -> B -> Y/O -> J -> M
 *         else: toggle tile collision
 * @endverbatim
 *
 * @par Per-frame pipeline
 * @code
 * Game::ProcessInput   -->  Editor::ProcessInput       (keyboard)
 *                      -->  Editor::ProcessMouseInput  (mouse)
 * Game::Update         -->  Editor::Update          (tile picker smoothing)
 * Game::Render         -->  Editor::Render          (overlays + tile picker)
 * Game::ScrollCallback -->  Editor::HandleScroll    (elevation / tile picker)
 * @endcode
 *
 * @par Debug overlays
 * When debug mode is active (toggled with `debug.overlays` in the developer
 * console, independently of editor mode), these overlays are rendered: collision,
 * navigation, stance, elevation, Y-sort flags, corner cutting, particle zones, NPC
 * patrol info, and a colour-coded highlight of layers 1-9. The structure overlay is
 * in the same call list but returns immediately unless EditMode::Structure is
 * selected, so debug mode alone never shows it; the stance overlay still draws the
 * auto-detected upright anchors.
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

    /**
     * @brief Check if the level editor is active.
     * @return `true` when the editor is active (toggled via `editor` console command).
     */
    [[nodiscard]] bool IsActive() const { return m_Active; }

    /**
     * @brief Push an on-screen status toast (e.g. from save/load).
     * @param message Text to display.
     * @param color Tint for the text (green for success, red for error).
     * @param durationSeconds How long the toast stays on screen.
     */
    void ShowStatus(std::string message, glm::vec3 color, float durationSeconds = 3.0f);

    /**
     * @brief Activate or deactivate the level editor.
     * @param active `true` to enable, `false` to disable.
     */
    void SetActive(bool active);

    /**
     * @brief Process keyboard input for editor hotkeys and mode switching.
     * @param deltaTime Frame time in seconds. Scales the arrow-key pan speed of the tile
     *                  picker (1000 px/s, 2.5x with Shift); unused when the picker is closed.
     *                  The world camera is panned by Game, not here.
     * @param ctx       Editor context providing window and game state.
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
     * Called from Game::Render() when editor or debug mode is active. The tile picker,
     * top bar, HUD and status toast each bind their own orthographic projection, so the
     * renderer is left in a UI projection whenever the editor is active.
     *
     * @post The world projection is not restored. The caller must rebind it before the
     *       next world draw; Game::Render does this immediately after the call.
     */
    void Render(const EditorContext& ctx);

    /**
     * @brief Render no-projection anchor markers on top of everything.
     *
     * Separate from Render() because anchors must appear above all UI. Does nothing
     * unless IsShowNoProjectionAnchors() is true. Game calls it only while the editor
     * is inactive: in editor mode RenderStanceOverlays already draws the same
     * auto-detected anchors.
     *
     * @pre The renderer is bound to the world projection.
     */
    void RenderNoProjectionAnchors(const EditorContext& ctx);

    /**
     * @brief Check if debug overlays are enabled.
     * @return `true` when debug overlays are visible (toggled via `debug.overlays`).
     */
    [[nodiscard]] bool IsDebugMode() const { return m_DebugMode; }

    /**
     * @brief Check if text debug info (FPS, coords) is shown.
     * @return `true` when debug text is visible.
     */
    [[nodiscard]] bool IsShowDebugInfo() const { return m_ShowDebugInfo; }

    /**
     * @brief Check if no-projection anchor markers are shown.
     * @return `true` when anchor markers are visible.
     */
    [[nodiscard]] bool IsShowNoProjectionAnchors() const { return m_ShowNoProjectionAnchors; }

    /**
     * @brief Check if the tile picker panel is visible.
     * @return `true` when the tile picker is open.
     */
    [[nodiscard]] bool IsShowTilePicker() const { return m_ShowTilePicker; }

    /// @brief Toggle debug overlay rendering on/off.
    void ToggleDebugMode();

    /// @brief Toggle text debug info display on/off.
    void ToggleShowDebugInfo();

    /**
     * @brief Set debug overlay rendering on/off explicitly.
     *
     * Also drives no-projection anchor-marker visibility: IsShowNoProjectionAnchors()
     * always equals IsDebugMode(), because nothing else writes that flag. There is no
     * independent toggle for the markers.
     */
    void SetDebugMode(bool enabled);

    /// @brief Set text debug info display on/off explicitly.
    void SetShowDebugInfo(bool enabled);

    /**
     * @brief Reset tile picker zoom and pan to defaults.
     *
     * Called from Game when Z key is pressed in editor mode.
     */
    void ResetTilePickerState();

    /**
     * @brief Drop all undo/redo history.
     *
     * Call this on level load before mutating the new tilemap, so stale commands cannot
     * revert against the wrong map.
     *
     * @warning Only the editor's own L-key reload calls it. The `map.load` console
     * command and Game::LoadGameWorld replace the tilemap without it, so commands
     * captured against the previous map stay on the stack and remain revertible. Any
     * new load path must call this itself.
     */
    void ClearUndoHistory();

private:
    /**
     * @name Render methods
     * @brief Private overlay and UI rendering routines.
     * @{
     */

    /// @brief Render editor UI elements (cursor, tile info, status text).
    void RenderEditorUI(const EditorContext& ctx);
    /// @brief Render persistent editor state HUD.
    void RenderEditorHUD(const EditorContext& ctx);
    /// @brief Render top-bar keyboard shortcuts reference (modes + actions).
    void RenderEditorTopBar(const EditorContext& ctx);
    /// @brief Render collision flag overlay.
    void RenderCollisionOverlays(const EditorContext& ctx);
    /// @brief Render navigation walkability overlay.
    void RenderNavigationOverlays(const EditorContext& ctx);
    /// @brief Render elevation value overlay.
    void RenderElevationOverlays(const EditorContext& ctx);
    /// @brief Render the per-tile stance overlay.
    void RenderStanceOverlays(const EditorContext& ctx);
    /// @brief Render no-projection anchor markers (implementation).
    void RenderNoProjectionAnchorsImpl(const EditorContext& ctx);
    /// @brief Render structure assignment overlay.
    void RenderStructureOverlays(const EditorContext& ctx);
    /**
     * @brief Render a generic per-layer flag overlay using a getter method pointer.
     *
     * @param ctx      Editor context.
     * @param editMode true = authoring view: draw only the cells whose flag is set on
     *                 m_CurrentLayer, at a fixed alpha of 0.5. false = debug summary:
     *                 count how many layers carry the flag at each cell and scale the
     *                 alpha by that fraction (0.15 + fraction * 0.35).
     * @param getter   Tilemap flag reader; takes a 0-based layer index.
     * @param color    RGB tint. Alpha comes from the branch above, not from the caller.
     */
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

    /// @brief Render the active Ctrl+drag selection rectangle overlay.
    void RenderMapSelectionOverlay(const EditorContext& ctx);

    /// @}

    /// @brief Clear all mutually exclusive edit sub-modes.
    void ClearAllEditModes();

    /// @brief Lazily compute and cache no-projection structure bounds for the current frame.
    void EnsureNoProjBoundsCache(const EditorContext& ctx);

    /**
     * @brief Map rotated tile offset to source tile coordinates.
     * @param dx Rotated offset X.
     * @param dy Rotated offset Y.
     * @param sourceDx Output source offset X.
     * @param sourceDy Output source offset Y.
     */
    void CalculateRotatedSourceTile(int dx, int dy, int& sourceDx, int& sourceDy) const;

    /// @brief Get tile rotation compensated for current editor rotation.
    float GetCompensatedTileRotation() const;

    /**
     * @struct ScreenToTile
     * @brief Mouse position converted from screen pixels into
     * world/tile coordinates.
     */
    struct ScreenToTile
    {
        float worldX = 0.0f;  ///< World X in pixels (camera position + zoomed screen offset).
        float worldY = 0.0f;  ///< World Y in pixels; +Y is down, matching the tilemap.
        /// Bare floor(world / tileSize); not clamped to the map, so it can be negative or
        /// past the last row/column. Callers bounds-check before touching the tilemap.
        int tileX = -1;
        int tileY = -1;
    };

    /**
     * @brief Convert screen-space mouse coordinates to world and tile coordinates.
     *
     * @warning Valid only on the flat 2.5D orthographic path: it inverts camera.position
     * and camera.zoom and nothing else. Under the `world3d` orbit camera Game::Render
     * skips Editor::Render entirely, yet Game::ProcessInput still drives the editor input,
     * so a click there edits a tile that matches nothing on screen.
     */
    [[nodiscard]] ScreenToTile ScreenToTileCoords(const EditorContext& ctx,
                                                  double mouseX,
                                                  double mouseY) const;

    /// @brief Track a map-changing edit for the HUD unsaved indicator.
    void MarkDirty() { m_HasUnsavedChanges = true; }
    /// @brief Clear the HUD unsaved indicator after successful save/load.
    void MarkClean() { m_HasUnsavedChanges = false; }

    /// @brief Execute an undoable command and mark the map dirty.
    void ExecuteEditorCommand(std::unique_ptr<EditorCommand> cmd,
                              Tilemap& tilemap,
                              ecs::registry& npcs);
    /// @brief Push an already-applied undoable command and mark the map dirty.
    void PushEditorCommand(std::unique_ptr<EditorCommand> cmd);

    /**
     * @brief Toggle a per-tile layer flag using the provided setter, with
     * single-tile or Shift+flood-fill behavior. Captures pre-mutation old
     * values so the caller can wrap the entries in the appropriate cmd
     * class (YSortPlus / YSortMinus share the entry shape).
     *
     * Mutates the tilemap in place. Returns the entries the caller should
     * push onto the undo stack as a per-mode cmd. Skips no-op entries (where
     * old == new) so the cmd only carries actual deltas.
     *
     * @param ctx Editor context.
     * @param tileX Tile column.
     * @param tileY Tile row.
     * @param getter Tilemap method pointer for reading the flag.
     * @param setter Tilemap method pointer for setting the flag.
     * @param newValue Target flag value (true to set, false to clear).
     * @param flagName Name of the flag (for debug display).
     */
    [[nodiscard]] std::vector<LayerFlagEntry> CollectYSortFlagToggle(
        const EditorContext& ctx,
        int tileX,
        int tileY,
        bool (Tilemap::*getter)(int, int, size_t) const,
        void (Tilemap::*setter)(int, int, size_t, bool),
        bool newValue,
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
        Stance,        ///< Painting per-tile TileStance (B key).
        YSortPlus,     ///< Editing Y-sort-plus flags (Y key).
        YSortMinus,    ///< Editing Y-sort-minus flags (O key).
        ParticleZone,  ///< Defining particle emitter zones (J key).
        Structure,     ///< Assigning tiles to structures (G key).
        Animation,     ///< Applying animations to tiles (K key).
    };

    /**
     * @name Mode state
     * @{
     */
    bool m_Active;          ///< Master toggle for the level editor (`editor` cmd).
    bool m_ShowTilePicker;  ///< Whether the tile picker panel is visible.
    EditMode m_EditMode;    ///< Current sub-mode (only one active at a time).

    /**
     * @brief Stance a left click paints in @c EditMode::Stance, cycled with , and .
     *
     * Never @c TileStance::Flat: right click is how a cell is cleared, so offering
     * Flat as a paintable value would give the same action two bindings and cost a
     * step in the cycle.
     *
     * @warning The cycle is modular arithmetic over the range 1..TILE_STANCE_COUNT-1.
     * A static_assert in TileStance.hpp pins Flat to 0, but nothing checks that the
     * paintable values stay contiguous: adding a non-paintable enumerator in the middle
     * makes , and . paint the wrong stance.
     */
    TileStance m_CurrentStance;
    /// @}

    /**
     * @name Particle zone editing
     * @brief State for drag-to-create particle emitter zones (J mode).
     * @{
     */
    ParticleType m_CurrentParticleType;  ///< Visual type for new zones (e.g. Firefly).
    bool m_ParticleNoProjection;         ///< If true, new zones skip perspective projection.
    bool m_PlacingParticleZone;          ///< True while the user is dragging to define a zone.
    glm::vec2 m_ParticleZoneStart;       ///< World position where the current drag began.
    /// @}

    /**
     * @name Structure editing
     * @brief State for the two-anchor structure workflow (G mode).
     *
     * Place the left anchor, then the right anchor, then flood-assign the tiles between
     * them to a structure ID.
     * @{
     */
    int m_CurrentStructureId;          ///< Active structure ID, or -1 if none selected.
    int m_PlacingAnchor;               ///< Anchor step: 0 = idle, 1 = left, 2 = right.
    glm::vec2 m_TempLeftAnchor;        ///< World position of left anchor (-1 = unset).
    glm::vec2 m_TempRightAnchor;       ///< World position of right anchor (-1 = unset).
    bool m_AssigningTilesToStructure;  ///< True during tile flood-assign phase.
    /// @}

    /**
     * @name Animation editing
     * @{
     */
    std::vector<int> m_AnimationFrames;  ///< Tile IDs composing the current animation sequence.
    float m_AnimationFrameDuration;      ///< Seconds each frame is shown (default 0.2s).
    int m_SelectedAnimationId;           ///< Index of the animation being edited, or -1.
    /// @}

    /**
     * @name Debug flags
     * @{
     */
    bool m_DebugMode;                ///< Enables all debug overlays (`debug.overlays` cmd).
    bool m_ShowDebugInfo;            ///< Shows text debug info (FPS, tile coords, etc.).
    bool m_ShowNoProjectionAnchors;  ///< Renders no-projection anchor markers on top of UI.
    bool m_HasUnsavedChanges;        ///< True after edits since the last successful save/load.
    /// @}

    /**
     * @name Editor chrome bands
     * @brief Heights of the opaque bands drawn over the world view, in screen pixels.
     *
     * @warning Neither band is excluded from mouse picking. ProcessMouseInput converts the
     * raw cursor position straight to a tile, so a click on a top-bar chip or on the HUD
     * also edits the tile drawn behind it.
     * @{
     */
    static constexpr float EDITOR_HUD_HEIGHT = 40.0f;     ///< Bottom HUD band height.
    static constexpr float EDITOR_TOPBAR_HEIGHT = 56.0f;  ///< Top shortcut-bar band height.
    /// @}

    /**
     * @name Status toast
     * @brief On-screen transient message (save success/failure, load result).
     *
     * Drawn while m_StatusTimer &gt; 0, which Update() decrements by the frame delta.
     * @{
     */
    std::string m_StatusMessage;       ///< Text to display; empty = hidden.
    float m_StatusTimer = 0.0f;        ///< Seconds remaining to display.
    glm::vec3 m_StatusColor{1, 1, 1};  ///< Tint (e.g. green for success, red for error).
    /// @}

    /**
     * @name Tile selection
     * @brief Currently selected tile, layer, and elevation for placement.
     * @{
     */
    /**
     * @brief Tile ID last clicked in the tile picker.
     *
     * While a picker drag is in progress it tracks the moving end corner of the selection
     * rectangle, with MultiTileState's selectionStartTileID as the anchor corner.
     */
    int m_SelectedTileID;
    /// Active tilemap layer index (0-based) for placement. Keys 1-9 and 0 select indices
    /// 0-9, so a map loaded with more than 10 dynamicLayers has no hotkey for the rest.
    int m_CurrentLayer;
    /**
     * @brief Elevation value painted in H mode, in pixels - written straight through to
     * Tilemap::SetElevation, where 0 means ground level.
     *
     * The scroll wheel steps it by 2 and clamps to [-32, +32]. Starts at 4, i.e. slightly
     * above ground, not at ground.
     */
    int m_CurrentElevation;
    /**
     * @brief Role a left click paints in @c EditMode::Elevation, cycled with , and .
     *
     * Never @c ElevationRole::Ground: right click is how a cell is cleared, so
     * offering Ground as a paintable value would give the same action two bindings.
     *
     * @warning The cycle is modular arithmetic over the range 1..ELEVATION_ROLE_COUNT-1.
     * A static_assert in ElevationRole.hpp pins Ground to 0, but nothing checks that the
     * paintable values stay contiguous: adding a non-paintable enumerator in the middle
     * makes , and . paint the wrong role.
     */
    ElevationRole m_CurrentElevationRole;
    /// @}

    /**
     * @name NPC types
     * @{
     */
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

    /**
     * @name Mouse/drag state
     * @brief Cursor position plus per-mode drag bookkeeping.
     *
     * The `last*Tile` pairs make drag-painting idempotent: a handler only acts when the
     * cursor has entered a different tile than the one it last wrote. They are reset to
     * the -1 sentinel ("no tile touched yet this drag") on the corresponding button-up.
     * The `*DragState` booleans latch the target value chosen by the first cell of a
     * right-drag so the whole stroke paints one value instead of toggling per cell.
     * @{
     */
    struct MouseDragState
    {
        /// Cursor position in window pixels as of the last ProcessMouseInput call. Currently
        /// write-only - nothing reads either field, so no behavior depends on them.
        double lastMouseX = 0.0;
        double lastMouseY = 0.0;
        /**
         * @brief Latched "left button already handled" flag.
         *
         * Set by whichever mode handler consumed the press so the remaining handlers skip
         * it, and to distinguish the first frame of a drag from its continuation. Cleared
         * on button-up.
         */
        bool mousePressed = false;
        /// Right-button analog of `mousePressed`; also gates the "first cell of the
        /// stroke" branch that decides `navigationDragState` / `collisionDragState`.
        bool rightMousePressed = false;
        int lastPlacedTileX = -1;          ///< Last tile column painted this left-drag (-1 = none).
        int lastPlacedTileY = -1;          ///< Last tile row painted this left-drag (-1 = none).
        int lastNavigationTileX = -1;      ///< Last tile column touched this nav drag (-1 = none).
        int lastNavigationTileY = -1;      ///< Last tile row touched this nav drag (-1 = none).
        bool navigationDragState = false;  ///< Walkability value the whole M-mode stroke writes.
        int lastCollisionTileX = -1;  ///< Last tile column touched this collision drag (-1 = none).
        int lastCollisionTileY = -1;  ///< Last tile row touched this collision drag (-1 = none).
        bool collisionDragState = false;  ///< Collision value the whole right-drag stroke writes.
        int lastNPCPlacementTileX = -1;   ///< Last tile column an NPC was placed on (-1 = none).
        int lastNPCPlacementTileY = -1;   ///< Last tile row an NPC was placed on (-1 = none).
    };
    MouseDragState m_Mouse;
    /// @}

    /**
     * @name Tile picker state
     * @brief Camera controls for the tile picker panel (zoom + smooth-scrolled offset).
     *
     * Pan input - arrow keys and a plain scroll - writes only `target*`; Update() eases the
     * live `offset*` toward it each frame and snaps once within 0.1px. Ctrl+scroll zoom and
     * ResetTilePickerState write both, so the change lands the same frame. SetActive equates
     * the two when the picker opens, so reopening does not replay a stale glide.
     * @{
     */
    struct TilePickerCamera
    {
        float zoom = 2.0f;     ///< Picker magnification, clamped to [0.25, 8] by Ctrl+scroll.
        float offsetX = 0.0f;  ///< Rendered pan X in screen pixels (eased toward targetOffsetX).
        float offsetY = 0.0f;  ///< Rendered pan Y in screen pixels (eased toward targetOffsetY).
        float targetOffsetX = 0.0f;  ///< Requested pan X; clamped so the sheet stays on screen.
        float targetOffsetY = 0.0f;  ///< Requested pan Y; clamped so the sheet stays on screen.
    };
    TilePickerCamera m_TilePicker;
    /// @}

    /**
     * @name Multi-tile selection
     * @brief Rectangular tile "brush" (stamp) selected from the picker.
     *
     * Drag inside the tile picker to define a rectangle; on release a region larger than
     * 1x1 arms `selectionMode` and resets the brush transform. Rotation and flips are
     * applied at paint/preview time by CalculateBrushSourceTile, so `width` / `height`
     * always describe the unrotated source region.
     * @{
     */
    struct MultiTileState
    {
        /// True once a region larger than 1x1 is armed: left-click stamps the whole brush
        /// instead of a single tile. Reset to false when a 1x1 region is picked.
        bool selectionMode = false;
        int selectedStartID = 0;        ///< Tile ID of the brush's top-left cell in the tileset.
        int width = 1;                  ///< Brush width in tiles, before rotation.
        int height = 1;                 ///< Brush height in tiles, before rotation.
        bool isSelecting = false;       ///< True while dragging out a rectangle in the picker.
        int selectionStartTileID = -1;  ///< Anchor corner of that drag (-1 = not dragging).
        /// Unused: never read or written anywhere in the editor.
        float placementCameraZoom = 1.0f;
        /// Write-only mirror of selectionMode; nothing reads it, so it drives no behavior.
        bool isPlacing = false;
        /// Brush rotation in degrees counter-clockwise: 0, 90, 180, or 270, cycled by R.
        /// At 90/270 the stamped footprint is height x width (the dimensions swap).
        int rotation = 0;
        bool flipX = false;  ///< Brush mirror around vertical axis (toggled by F).
        bool flipY = false;  ///< Brush mirror around horizontal axis (toggled by Shift+F).
    };
    MultiTileState m_MultiTile;
    /// @}

    /**
     * @name Key debounce state
     * @brief Per-key pressed tracking for edge-triggered input.
     *
     * Held per instance rather than in function-local statics, so two Editor
     * instances (or a reload) cannot share debounce state.
     * @{
     */
    std::bitset<GLFW_KEY_LAST + 1> m_KeyPressed;  ///< True while a key is held from last press.
    int m_LastDeletedTileX;                       ///< Last tile column erased during delete-drag.
    int m_LastDeletedTileY;                       ///< Last tile row erased during delete-drag.
    /// @}

    /**
     * @name No-projection bounds cache
     * @brief Connected no-projection groups, computed once per frame.
     *
     * EnsureNoProjBoundsCache scans the whole map once and 4-connected flood-fills every
     * cell whose stance is @c TileStance::Structure on any layer. Each component becomes
     * its own entry, so the vector holds one inclusive tile bounding box per group. The
     * cache exists so the several overlay passes in a single Render() share that one scan
     * instead of repeating it.
     * @{
     */

    /// Inclusive tile-space bounding box of one connected structure-stance group.
    struct NoProjGroupBounds
    {
        int minX, maxX, minY, maxY;
    };
    std::vector<NoProjGroupBounds> m_CachedNoProjBounds;  ///< One entry per connected group.
    bool m_NoProjBoundsCached = false;  ///< Reset at the start of each Render() call.
    /// @}

    /**
     * @name Undo / redo
     * @brief Bounded command-pattern history plus the drag-paint stroke accumulators.
     *
     * Default capacity is UndoRedoStack::DEFAULT_CAPACITY. Only the editor's own L-key
     * reload clears the stack, through ClearUndoHistory; the `map.load` console command
     * and Game::LoadGameWorld do not. Stroke accumulators batch per-tile mutations during
     * a drag-paint and commit a single composite cmd at mouse-up; Drop() in
     * ClearAllEditModes discards an in-progress stroke when the mode changes mid-drag.
     * @{
     */
    UndoRedoStack m_UndoStack;                 ///< Bounded history; owns all pushed commands.
    TilePlaceStrokeAccum m_TileStroke;         ///< Default-mode left-drag tile painting.
    CollisionStrokeAccum m_CollisionStroke;    ///< Default-mode right-drag collision painting.
    ElevationStrokeAccum m_ElevationStroke;    ///< H-mode left-drag elevation painting.
    NavigationStrokeAccum m_NavigationStroke;  ///< M-mode right-drag walkability painting.
    /// @}

    /**
     * @name Region copy/paste (Ctrl+drag, Ctrl+C, Ctrl+V)
     * @brief On-map rectangular selection and the copied-region clipboard.
     *
     * MapRegionSelection tracks the current rectangular tile-region the user has selected
     * on the map (Ctrl+left-drag to define). m_Clipboard holds the most recently copied
     * region; PasteRegionCmd writes from there.
     * @{
     */

    /**
     * @brief Rectangular map selection.
     *
     * The corners are stored as picked, so `start` may be to the right of / below `end`;
     * the accessors below normalize that. Coordinates are tile indices and both ends are
     * inclusive, hence the +1 in Width()/Height().
     */
    struct MapRegionSelection
    {
        bool active = false;      ///< A committed selection exists (Ctrl+C / overlay use it).
        bool isDragging = false;  ///< A drag is in progress; ends on left-button release.
        int startX = 0;           ///< Anchor corner column (where the drag began).
        int startY = 0;           ///< Anchor corner row.
        int endX = 0;             ///< Moving corner column (frozen if Ctrl is let go mid-drag).
        int endY = 0;             ///< Moving corner row.

        [[nodiscard]] int MinX() const { return std::min(startX, endX); }
        [[nodiscard]] int MinY() const { return std::min(startY, endY); }
        [[nodiscard]] int Width() const { return std::abs(endX - startX) + 1; }
        [[nodiscard]] int Height() const { return std::abs(endY - startY) + 1; }
    };
    MapRegionSelection m_MapSelection;
    ClipboardRegion m_Clipboard;  ///< Last Ctrl+C snapshot; empty until the first copy.
    /// @}
};
