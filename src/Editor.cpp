#include "Editor.h"

#include "Logger.h"
#include "MathUtils.h"
#include "NavigationRecalc.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Editor";
}  // namespace

static constexpr glm::vec4 LAYER_COLORS[] = {
    {0.0f, 0.0f, 0.0f, 0.0f},  // layer 0 - transparent (Ground, unused)
    {0.2f, 0.5f, 1.0f, 0.4f},  // layer 1 - blue (Ground Detail)
    {0.2f, 1.0f, 0.2f, 0.4f},  // layer 2 - green (Objects)
    {1.0f, 0.2f, 0.8f, 0.4f},  // layer 3 - magenta (Objects2)
    {1.0f, 0.5f, 0.0f, 0.4f},  // layer 4 - orange (Objects3)
    {1.0f, 1.0f, 0.2f, 0.4f},  // layer 5 - yellow (Foreground)
    {0.2f, 1.0f, 1.0f, 0.4f},  // layer 6 - cyan (Foreground2)
    {1.0f, 0.3f, 0.3f, 0.4f},  // layer 7 - red (Overlay)
    {1.0f, 0.3f, 1.0f, 0.4f},  // layer 8 - magenta (Overlay2)
    {1.0f, 1.0f, 1.0f, 0.4f},  // layer 9 - white (Overlay3)
};

Editor::Editor()
    // -- Mode flags: all sub-modes start inactive --
    : m_Active(false),
      m_ShowTilePicker(false),
      m_EditMode(EditMode::None),

      // -- Particle zone editing --
      m_CurrentParticleType(ParticleType::Firefly),  // default particle visual
      m_ParticleNoProjection(false),
      m_PlacingParticleZone(false),  // true while dragging to define a zone
      m_ParticleZoneStart(0.0f, 0.0f),

      // -- Structure editing: -1 means no structure selected / no anchor placed --
      m_CurrentStructureId(-1),
      m_PlacingAnchor(0),  // 0 = not placing, 1 = left anchor, 2 = right anchor
      m_TempLeftAnchor(-1.0f, -1.0f),
      m_TempRightAnchor(-1.0f, -1.0f),
      m_AssigningTilesToStructure(false),

      // -- Animation editing --
      m_AnimationFrameDuration(0.2f),  // seconds per frame
      m_SelectedAnimationId(-1),       // -1 = none selected

      // -- Debug flags --
      m_DebugMode(false),
      m_ShowDebugInfo(false),
      m_ShowNoProjectionAnchors(false),
      m_HasUnsavedChanges(false),

      // -- Tile selection: layer 0, elevation 4 is the default ground level --
      m_SelectedTileID(0),
      m_CurrentLayer(0),
      m_CurrentElevation(4),

      // -- NPC placement --
      m_SelectedNPCTypeIndex(0),

      // -- Mouse / drag, tile picker, multi-tile: use default member initializers --
      m_Mouse{},
      m_TilePicker{},
      m_MultiTile{},

      // -- Key debounce: m_KeyPressed default-constructs to all-zero --
      m_LastDeletedTileX(-1),
      m_LastDeletedTileY(-1)
{
}

void Editor::Initialize(const std::vector<std::string>& npcTypes)
{
    m_AvailableNPCTypes = npcTypes;
    m_SelectedNPCTypeIndex = 0;

    if (!m_AvailableNPCTypes.empty())
    {
        std::ostringstream line;
        line << "Available NPC types: ";
        for (size_t i = 0; i < m_AvailableNPCTypes.size(); ++i)
        {
            line << m_AvailableNPCTypes[i];
            if (i == m_SelectedNPCTypeIndex)
            {
                line << " (selected)";
            }
            if (i < m_AvailableNPCTypes.size() - 1)
            {
                line << ", ";
            }
        }
        Logger::Info(LOG_SUBSYSTEM, line.str());
    }
}

void Editor::SetActive(bool active)
{
    m_Active = active;
    if (active)
    {
        m_ShowTilePicker = true;
        m_TilePicker.targetOffsetX = m_TilePicker.offsetX;
        m_TilePicker.targetOffsetY = m_TilePicker.offsetY;
    }
    else
    {
        m_ShowTilePicker = false;
    }
}

void Editor::ToggleDebugMode()
{
    SetDebugMode(!m_DebugMode);
}

void Editor::ToggleShowDebugInfo()
{
    SetShowDebugInfo(!m_ShowDebugInfo);
}

void Editor::SetDebugMode(bool enabled)
{
    m_DebugMode = enabled;
    m_ShowNoProjectionAnchors = m_DebugMode;
    Logger::InfoF(LOG_SUBSYSTEM, "Debug mode: {}", m_DebugMode ? "ON" : "OFF");
}

void Editor::SetShowDebugInfo(bool enabled)
{
    m_ShowDebugInfo = enabled;
    Logger::InfoF(LOG_SUBSYSTEM, "Debug info display: {}", m_ShowDebugInfo ? "ON" : "OFF");
}

void Editor::ResetTilePickerState()
{
    m_TilePicker.zoom = 2.0f;
    m_TilePicker.offsetX = 0.0f;
    m_TilePicker.offsetY = 0.0f;
    m_TilePicker.targetOffsetX = 0.0f;
    m_TilePicker.targetOffsetY = 0.0f;
    Logger::Info(LOG_SUBSYSTEM, "Tile picker zoom and offset reset to defaults");
}

void Editor::Update(float deltaTime, const EditorContext& ctx)
{
    // Smooth tile picker camera movement
    if (m_Active && m_ShowTilePicker)
    {
        // Exponential decay smoothing for tile picker pan
        float dt = rift::ExpApproachAlpha(deltaTime, 0.16f);

        m_TilePicker.offsetX =
            m_TilePicker.offsetX + (m_TilePicker.targetOffsetX - m_TilePicker.offsetX) * dt;
        m_TilePicker.offsetY =
            m_TilePicker.offsetY + (m_TilePicker.targetOffsetY - m_TilePicker.offsetY) * dt;

        if (std::abs(m_TilePicker.targetOffsetX - m_TilePicker.offsetX) < 0.1f)
        {
            m_TilePicker.offsetX = m_TilePicker.targetOffsetX;
        }
        if (std::abs(m_TilePicker.targetOffsetY - m_TilePicker.offsetY) < 0.1f)
        {
            m_TilePicker.offsetY = m_TilePicker.targetOffsetY;
        }
    }
    else
    {
        m_TilePicker.offsetX = m_TilePicker.targetOffsetX;
        m_TilePicker.offsetY = m_TilePicker.targetOffsetY;
    }

    // Fade out the status toast.
    if (m_StatusTimer > 0.0f)
    {
        m_StatusTimer = std::max(0.0f, m_StatusTimer - deltaTime);
        if (m_StatusTimer <= 0.0f)
            m_StatusMessage.clear();
    }
}

void Editor::ShowStatus(std::string message, glm::vec3 color, float durationSeconds)
{
    m_StatusMessage = std::move(message);
    m_StatusColor = color;
    m_StatusTimer = durationSeconds;
}

void Editor::ClearUndoHistory()
{
    m_UndoStack.Clear();
}

Editor::ScreenToTile Editor::ScreenToTileCoords(const EditorContext& ctx,
                                                double mouseX,
                                                double mouseY) const
{
    if (ctx.screenWidth <= 0 || ctx.screenHeight <= 0 || ctx.tilemap.GetTileWidth() <= 0 ||
        ctx.tilemap.GetTileHeight() <= 0)
    {
        return {};
    }

    float zoom = std::max(ctx.cameraZoom, 0.01f);
    float worldW = static_cast<float>(ctx.tilesVisibleWidth * ctx.tilemap.GetTileWidth()) / zoom;
    float worldH = static_cast<float>(ctx.tilesVisibleHeight * ctx.tilemap.GetTileHeight()) / zoom;
    float worldX = (static_cast<float>(mouseX) / static_cast<float>(ctx.screenWidth)) * worldW +
                   ctx.cameraPosition.x;
    float worldY = (static_cast<float>(mouseY) / static_cast<float>(ctx.screenHeight)) * worldH +
                   ctx.cameraPosition.y;

    return {worldX,
            worldY,
            static_cast<int>(std::floor(worldX / ctx.tilemap.GetTileWidth())),
            static_cast<int>(std::floor(worldY / ctx.tilemap.GetTileHeight()))};
}

void Editor::ExecuteEditorCommand(std::unique_ptr<EditorCommand> cmd,
                                  Tilemap& tilemap,
                                  std::vector<NonPlayerCharacter>& npcs)
{
    if (!cmd)
        return;

    m_UndoStack.Execute(std::move(cmd), tilemap, npcs);
    MarkDirty();
}

void Editor::PushEditorCommand(std::unique_ptr<EditorCommand> cmd)
{
    if (!cmd)
        return;

    m_UndoStack.Push(std::move(cmd));
    MarkDirty();
}

void Editor::ClearAllEditModes()
{
    // Reset every transient per-mode flag. Must be called before entering or
    // leaving any mode so that, e.g., a half-drawn particle zone or half-
    // placed structure anchor can't outlive its owning mode and render a
    // ghost preview forever.
    m_EditMode = EditMode::None;

    // Particle zone drag state
    m_PlacingParticleZone = false;

    // Structure anchor/flood state
    m_PlacingAnchor = 0;
    m_TempLeftAnchor = glm::vec2(-1.0f, -1.0f);
    m_TempRightAnchor = glm::vec2(-1.0f, -1.0f);
    m_AssigningTilesToStructure = false;

    // Animation editing state
    m_AnimationFrames.clear();
    m_SelectedAnimationId = -1;

    // Drag state (a mid-drag mode switch invalidates the drag)
    m_Mouse.mousePressed = false;
    m_Mouse.rightMousePressed = false;

    // Drop any in-progress stroke accumulators. Tiles already painted during
    // the discarded drag stay (consistent with prior behavior - the drag
    // mutated the tilemap frame-by-frame); they just don't produce an
    // undoable command. Documented in EDITOR.md.
    m_TileStroke.Drop();
    m_CollisionStroke.Drop();
    m_ElevationStroke.Drop();
    m_NavigationStroke.Drop();
}

void Editor::Render(const EditorContext& ctx)
{
    m_NoProjBoundsCached = false;
    // Render editor tile picker UI
    if (m_Active && m_ShowTilePicker)
    {
        IRenderer::PerspectiveSuspendGuard guard(ctx.renderer);
        RenderEditorUI(ctx);
    }

    // Status toast (save success/failure, load result). Rendered on top of
    // everything so the user actually sees it - this is what distinguishes
    // "save silently failed" from "save succeeded".
    if (m_Active && m_StatusTimer > 0.0f && !m_StatusMessage.empty())
    {
        IRenderer::PerspectiveSuspendGuard guard(ctx.renderer);
        glm::mat4 uiProjection = glm::ortho(0.0f,
                                            static_cast<float>(ctx.screenWidth),
                                            static_cast<float>(ctx.screenHeight),
                                            0.0f,
                                            -1.0f,
                                            1.0f);
        ctx.renderer.SetProjection(uiProjection);
        const glm::vec2 pos(20.0f,
                            static_cast<float>(ctx.screenHeight) - EDITOR_HUD_HEIGHT - 24.0f);
        ctx.renderer.DrawText(m_StatusMessage, pos, 0.5f, m_StatusColor);
    }

    // Shared overlays: rendered once when either editor or debug mode is active
    if ((m_Active || m_DebugMode) && !m_ShowTilePicker)
    {
        RenderCollisionOverlays(ctx);
        RenderNavigationOverlays(ctx);
        RenderNoProjectionOverlays(ctx);
        RenderStructureOverlays(ctx);
        RenderYSortPlusOverlays(ctx);
        RenderYSortMinusOverlays(ctx);
    }

    // Editor-only overlays: layer highlight and placement preview
    if (m_Active && !m_ShowTilePicker)
    {
        if (!m_DebugMode && m_CurrentLayer >= 1 && m_CurrentLayer <= 9)
        {
            RenderLayerOverlay(ctx, m_CurrentLayer, LAYER_COLORS[m_CurrentLayer]);
        }

        RenderPlacementPreview(ctx);
        RenderMapSelectionOverlay(ctx);
    }

    // Debug-only overlays: extra visualizations not shown in normal editor mode
    if (m_DebugMode && !m_ShowTilePicker)
    {
        RenderCornerCuttingOverlays(ctx);
        RenderElevationOverlays(ctx);
        RenderParticleZoneOverlays(ctx);
        RenderNPCDebugInfo(ctx);

        for (int i = 1; i <= 9; ++i)
        {
            RenderLayerOverlay(ctx, i, LAYER_COLORS[i]);
        }
    }

    if (m_Active)
    {
        IRenderer::PerspectiveSuspendGuard guard(ctx.renderer);
        RenderEditorHUD(ctx);
    }
}

void Editor::RenderNoProjectionAnchors(const EditorContext& ctx)
{
    RenderNoProjectionAnchorsImpl(ctx);
}

void Editor::RecalculateNPCPatrolRoutes(const EditorContext& ctx)
{
    // Thin wrapper - the body lives in NavigationRecalc.{h,cpp} so editor
    // commands can invoke the same logic without coupling to Editor itself.
    auto displaced = SnapshotAndEraseNPCsOnNonWalkable(ctx.tilemap, ctx.npcs);
    (void)displaced;  // intentionally discarded - this code path is non-undoable
    RebuildPatrolRoutes(ctx.tilemap, ctx.npcs);
}
