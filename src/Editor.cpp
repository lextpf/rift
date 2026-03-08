#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <iostream>

static constexpr glm::vec4 kLayerColors[] = {
    {0.0f, 0.0f, 0.0f, 0.0f},  // layer 0 (ground, unused)
    {0.2f, 0.5f, 1.0f, 0.4f},  // layer 1 -- blue (Ground Detail)
    {0.2f, 1.0f, 0.2f, 0.4f},  // layer 2 -- green (Objects)
    {1.0f, 0.2f, 0.8f, 0.4f},  // layer 3 -- magenta (Objects2)
    {1.0f, 0.5f, 0.0f, 0.4f},  // layer 4 -- orange (Objects3)
    {1.0f, 1.0f, 0.2f, 0.4f},  // layer 5 -- yellow (Foreground)
    {0.2f, 1.0f, 1.0f, 0.4f},  // layer 6 -- cyan (Foreground2)
    {1.0f, 0.3f, 0.3f, 0.4f},  // layer 7 -- red (Overlay)
    {1.0f, 0.3f, 1.0f, 0.4f},  // layer 8 -- magenta (Overlay2)
    {1.0f, 1.0f, 1.0f, 0.4f},  // layer 9 -- white (Overlay3)
};

Editor::Editor()
    : m_EditorMode(false),
      m_ShowTilePicker(false),
      m_EditNavigationMode(false),
      m_ElevationEditMode(false),
      m_NPCPlacementMode(false),
      m_NoProjectionEditMode(false),
      m_YSortPlusEditMode(false),
      m_YSortMinusEditMode(false),
      m_ParticleZoneEditMode(false),
      m_StructureEditMode(false),
      m_AnimationEditMode(false),
      m_CurrentParticleType(ParticleType::Firefly),
      m_ParticleNoProjection(false),
      m_PlacingParticleZone(false),
      m_ParticleZoneStart(0.0f, 0.0f),
      m_CurrentStructureId(-1),
      m_PlacingAnchor(0),
      m_TempLeftAnchor(-1.0f, -1.0f),
      m_TempRightAnchor(-1.0f, -1.0f),
      m_AssigningTilesToStructure(false),
      m_AnimationFrameDuration(0.2f),
      m_SelectedAnimationId(-1),
      m_DebugMode(false),
      m_ShowDebugInfo(false),
      m_ShowNoProjectionAnchors(false),
      m_SelectedTileID(0),
      m_CurrentLayer(0),
      m_CurrentElevation(4),
      m_SelectedNPCTypeIndex(0),
      m_LastMouseX(0.0),
      m_LastMouseY(0.0),
      m_MousePressed(false),
      m_RightMousePressed(false),
      m_LastPlacedTileX(-1),
      m_LastPlacedTileY(-1),
      m_LastNavigationTileX(-1),
      m_LastNavigationTileY(-1),
      m_NavigationDragState(false),
      m_LastCollisionTileX(-1),
      m_LastCollisionTileY(-1),
      m_CollisionDragState(false),
      m_LastNPCPlacementTileX(-1),
      m_LastNPCPlacementTileY(-1),
      m_TilePickerZoom(2.0f),
      m_TilePickerOffsetX(0.0f),
      m_TilePickerOffsetY(0.0f),
      m_TilePickerTargetOffsetX(0.0f),
      m_TilePickerTargetOffsetY(0.0f),
      m_MultiTileSelectionMode(false),
      m_SelectedTileStartID(0),
      m_SelectedTileWidth(1),
      m_SelectedTileHeight(1),
      m_IsSelectingTiles(false),
      m_SelectionStartTileID(-1),
      m_PlacementCameraZoom(1.0f),
      m_IsPlacingMultiTile(false),
      m_MultiTileRotation(0)
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
    m_EditorMode = active;
    if (active)
    {
        m_ShowTilePicker = true;
        m_TilePickerTargetOffsetX = m_TilePickerOffsetX;
        m_TilePickerTargetOffsetY = m_TilePickerOffsetY;
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
    m_TilePickerZoom = 2.0f;
    m_TilePickerOffsetX = 0.0f;
    m_TilePickerOffsetY = 0.0f;
    m_TilePickerTargetOffsetX = 0.0f;
    m_TilePickerTargetOffsetY = 0.0f;
    std::cout << "Tile picker zoom and offset reset to defaults" << std::endl;
}

void Editor::Update(float deltaTime, const EditorContext& ctx)
{
    // Smooth tile picker camera movement
    if (m_EditorMode && m_ShowTilePicker)
    {
        // Exponential decay smoothing for tile picker pan
        auto expApproachAlpha = [](float dt, float st, float e = 0.01f) -> float
        {
            dt = std::max(0.0f, dt);
            st = std::max(1e-5f, st);
            return std::clamp(1.0f - std::pow(e, dt / st), 0.0f, 1.0f);
        };
        float dt = expApproachAlpha(deltaTime, 0.16f);

        m_TilePickerOffsetX =
            m_TilePickerOffsetX + (m_TilePickerTargetOffsetX - m_TilePickerOffsetX) * dt;
        m_TilePickerOffsetY =
            m_TilePickerOffsetY + (m_TilePickerTargetOffsetY - m_TilePickerOffsetY) * dt;

        if (std::abs(m_TilePickerTargetOffsetX - m_TilePickerOffsetX) < 0.1f)
        {
            m_TilePickerOffsetX = m_TilePickerTargetOffsetX;
        }
        if (std::abs(m_TilePickerTargetOffsetY - m_TilePickerOffsetY) < 0.1f)
        {
            m_TilePickerOffsetY = m_TilePickerTargetOffsetY;
        }
    }
    else
    {
        m_TilePickerOffsetX = m_TilePickerTargetOffsetX;
        m_TilePickerOffsetY = m_TilePickerTargetOffsetY;
    }
}

void Editor::Render(const EditorContext& ctx)
{
    // Render editor tile picker UI
    if (m_EditorMode && m_ShowTilePicker)
    {
        ctx.renderer.SuspendPerspective(true);
        RenderEditorUI(ctx);
        ctx.renderer.SuspendPerspective(false);
    }

    // Render overlays when editor mode is on and tile picker is hidden
    if (m_EditorMode && !m_ShowTilePicker)
    {
        RenderCollisionOverlays(ctx);
        RenderNavigationOverlays(ctx);
        RenderNoProjectionOverlays(ctx);
        RenderStructureOverlays(ctx);
        RenderYSortPlusOverlays(ctx);
        RenderYSortMinusOverlays(ctx);
    }

    // Layer overlays when respective layer is selected
    if (m_EditorMode && !m_ShowTilePicker)
    {
        if (m_CurrentLayer >= 1 && m_CurrentLayer <= 9)
            RenderLayerOverlay(ctx, m_CurrentLayer, kLayerColors[m_CurrentLayer]);

        RenderPlacementPreview(ctx);
    }

    // Debug mode overlays (F3) - show all overlays regardless of editor mode
    if (m_DebugMode && !m_ShowTilePicker)
    {
        RenderCollisionOverlays(ctx);
        RenderNavigationOverlays(ctx);
        RenderCornerCuttingOverlays(ctx);
        RenderElevationOverlays(ctx);
        RenderNoProjectionOverlays(ctx);
        RenderStructureOverlays(ctx);
        RenderYSortPlusOverlays(ctx);
        RenderYSortMinusOverlays(ctx);
        RenderParticleZoneOverlays(ctx);
        RenderNPCDebugInfo(ctx);

        for (int i = 1; i <= 9; ++i)
            RenderLayerOverlay(ctx, i, kLayerColors[i]);
    }
}

void Editor::RenderNoProjectionAnchors(const EditorContext& ctx)
{
    RenderNoProjectionAnchorsImpl(ctx);
}

void Editor::RecalculateNPCPatrolRoutes(const EditorContext& ctx)
{
    for (size_t i = 0; i < ctx.npcs.size();)
    {
        auto& npc = ctx.npcs[i];
        bool hasNavigation = ctx.tilemap.GetNavigation(npc.GetTileX(), npc.GetTileY());
        if (!hasNavigation)
        {
            std::cout << "Removing NPC at tile (" << npc.GetTileX() << ", " << npc.GetTileY()
                      << ") - no longer on navigation" << std::endl;
            ctx.npcs.erase(ctx.npcs.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        if (!npc.ReinitializePatrolRoute(&ctx.tilemap))
        {
            std::cout << "Warning: NPC at (" << npc.GetTileX() << ", " << npc.GetTileY()
                      << ") could not find valid patrol route" << std::endl;
        }
        ++i;
    }
}
