#include "Editor.h"
#include "MathUtils.h"

#include <algorithm>
#include <cmath>
#include <iostream>

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
        std::cout << "Available NPC types: ";
        for (size_t i = 0; i < m_AvailableNPCTypes.size(); ++i)
        {
            std::cout << m_AvailableNPCTypes[i];
            if (i == m_SelectedNPCTypeIndex)
                std::cout << " (selected)";
            if (i < m_AvailableNPCTypes.size() - 1)
                std::cout << ", ";
        }
        std::cout << std::endl;
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
    m_DebugMode = !m_DebugMode;
    m_ShowNoProjectionAnchors = m_DebugMode;
    std::cout << "Debug mode: " << (m_DebugMode ? "ON" : "OFF") << std::endl;
}

void Editor::ToggleShowDebugInfo()
{
    m_ShowDebugInfo = !m_ShowDebugInfo;
    std::cout << "Debug info display: " << (m_ShowDebugInfo ? "ON" : "OFF") << std::endl;
}

void Editor::ResetTilePickerState()
{
    m_TilePicker.zoom = 2.0f;
    m_TilePicker.offsetX = 0.0f;
    m_TilePicker.offsetY = 0.0f;
    m_TilePicker.targetOffsetX = 0.0f;
    m_TilePicker.targetOffsetY = 0.0f;
    std::cout << "Tile picker zoom and offset reset to defaults" << std::endl;
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
    // everything so the user actually sees it — this is what distinguishes
    // "save silently failed" from "save succeeded".
    if (m_Active && m_StatusTimer > 0.0f && !m_StatusMessage.empty())
    {
        IRenderer::PerspectiveSuspendGuard guard(ctx.renderer);
        const glm::vec2 pos(20.0f, static_cast<float>(ctx.screenHeight) - 40.0f);
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
}

void Editor::RenderNoProjectionAnchors(const EditorContext& ctx)
{
    RenderNoProjectionAnchorsImpl(ctx);
}

void Editor::RecalculateNPCPatrolRoutes(const EditorContext& ctx)
{
    std::erase_if(ctx.npcs,
                  [&](const NonPlayerCharacter& npc)
                  {
                      bool remove = !ctx.tilemap.GetNavigation(npc.GetTileX(), npc.GetTileY());
                      if (remove)
                      {
                          std::cout << "Removing NPC at tile (" << npc.GetTileX() << ", "
                                    << npc.GetTileY() << ") - no longer on navigation" << std::endl;
                      }
                      return remove;
                  });

    for (auto& npc : ctx.npcs)
    {
        if (!npc.ReinitializePatrolRoute(&ctx.tilemap))
        {
            std::cout << "Warning: NPC at (" << npc.GetTileX() << ", " << npc.GetTileY()
                      << ") could not find valid patrol route" << std::endl;
        }
    }
}
