#include "Dialogues.h"
#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace
{

template <typename ConditionFn, typename ActionFn>
int FloodFill(
    Tilemap& tilemap, int startX, int startY, ConditionFn shouldProcess, ActionFn applyAction)
{
    int mapWidth = tilemap.GetMapWidth();
    int mapHeight = tilemap.GetMapHeight();
    std::vector<bool> visited(static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight),
                              false);
    std::vector<std::pair<int, int>> stack;
    stack.push_back({startX, startY});
    int count = 0;
    while (!stack.empty())
    {
        auto [cx, cy] = stack.back();
        stack.pop_back();
        if (cx < 0 || cx >= mapWidth || cy < 0 || cy >= mapHeight)
            continue;
        size_t idx = static_cast<size_t>(cy * mapWidth + cx);
        if (visited[idx])
            continue;
        if (!shouldProcess(cx, cy))
            continue;
        visited[idx] = true;
        applyAction(cx, cy);
        count++;
        stack.push_back({cx - 1, cy});
        stack.push_back({cx + 1, cy});
        stack.push_back({cx, cy - 1});
        stack.push_back({cx, cy + 1});
    }
    return count;
}

struct ScreenToTile
{
    float worldX, worldY;
    int tileX, tileY;
};

ScreenToTile ScreenToTileCoords(const EditorContext& ctx, double mouseX, double mouseY)
{
    // Convert screen pixels to world coordinates:
    // 1. Compute the world-space viewport size (base tile area divided by zoom)
    // 2. Map mouse pixel position [0, screenSize] -> [0, worldSize] via ratio
    // 3. Add camera offset to get absolute world position
    // 4. Floor-divide by tile size to get the tile index
    float worldW =
        static_cast<float>(ctx.tilesVisibleWidth * ctx.tilemap.GetTileWidth()) / ctx.cameraZoom;
    float worldH =
        static_cast<float>(ctx.tilesVisibleHeight * ctx.tilemap.GetTileHeight()) / ctx.cameraZoom;
    float worldX = (static_cast<float>(mouseX) / static_cast<float>(ctx.screenWidth)) * worldW +
                   ctx.cameraPosition.x;
    float worldY = (static_cast<float>(mouseY) / static_cast<float>(ctx.screenHeight)) * worldH +
                   ctx.cameraPosition.y;
    return {worldX,
            worldY,
            static_cast<int>(std::floor(worldX / ctx.tilemap.GetTileWidth())),
            static_cast<int>(std::floor(worldY / ctx.tilemap.GetTileHeight()))};
}

}  // anonymous namespace

void Editor::ProcessInput(float deltaTime, const EditorContext& ctx)
{
    if (glfwGetKey(ctx.window, GLFW_KEY_T) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_T] && m_Active)
    {
        m_ShowTilePicker = !m_ShowTilePicker;
        m_KeyPressed[GLFW_KEY_T] = true;
        std::cout << "Tile picker: " << (m_ShowTilePicker ? "SHOWN" : "HIDDEN") << std::endl;

        if (m_ShowTilePicker)
        {
            // Sync smooth scrolling state to prevent jump
            m_TilePicker.targetOffsetX = m_TilePicker.offsetX;
            m_TilePicker.targetOffsetY = m_TilePicker.offsetY;
            std::vector<int> validTiles = ctx.tilemap.GetValidTileIDs();
            std::cout << "Total valid tiles available: " << validTiles.size() << std::endl;
            std::cout << "Currently selected tile ID: " << m_SelectedTileID << std::endl;
        }
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_T) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_T] = false;
    }

    // Rotates the selected tile(s) by 90 increments (0 -> 90 -> 180 -> 270).
    // Works for both single tiles and multi-tile selections when tile picker is closed.
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_R] && m_Active &&
        !m_ShowTilePicker)
    {
        m_MultiTile.rotation = (m_MultiTile.rotation + 90) % 360;
        m_KeyPressed[GLFW_KEY_R] = true;
        std::cout << "Tile rotation: " << m_MultiTile.rotation << " degrees" << std::endl;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_R] = false;
    }

    // Pans the tile picker view using arrow keys. Shift increases speed 2.5x.
    // Uses smooth scrolling with target-based interpolation.
    if (m_Active && m_ShowTilePicker)
    {
        float scrollSpeed = 1000.0f * deltaTime;

        // Shift modifier for faster navigation (2.5x speed)
        if (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        {
            scrollSpeed *= 2.5f;
        }

        // Arrow key input
        if (glfwGetKey(ctx.window, GLFW_KEY_UP) == GLFW_PRESS)
        {
            m_TilePicker.targetOffsetY += scrollSpeed;  // Scroll down (view up)
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_DOWN) == GLFW_PRESS)
        {
            m_TilePicker.targetOffsetY -= scrollSpeed;  // Scroll up (view down)
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_LEFT) == GLFW_PRESS)
        {
            m_TilePicker.targetOffsetX += scrollSpeed;  // Scroll right (view left)
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        {
            m_TilePicker.targetOffsetX -= scrollSpeed;  // Scroll left (view right)
        }

        // Calculate tile picker layout dimensions
        int dataTilesPerRow = ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();
        int dataTilesPerCol = ctx.tilemap.GetTilesetDataHeight() / ctx.tilemap.GetTileHeight();

        // Tile display size: base size * zoom factor
        // Base size is calculated to fit all tiles horizontally with 1.5x padding
        float baseTileSizePixels =
            (static_cast<float>(ctx.screenWidth) / static_cast<float>(dataTilesPerRow)) * 1.5f;
        float tileSizePixels = baseTileSizePixels * m_TilePicker.zoom;

        // Total content dimensions
        float totalTilesWidth = tileSizePixels * dataTilesPerRow;
        float totalTilesHeight = tileSizePixels * dataTilesPerCol;

        // Clamp offset bounds to prevent scrolling beyond content edges
        float minOffsetX = std::min(0.0f, ctx.screenWidth - totalTilesWidth);
        float maxOffsetX = 0.0f;
        float minOffsetY = std::min(0.0f, ctx.screenHeight - totalTilesHeight);
        float maxOffsetY = 0.0f;

        m_TilePicker.targetOffsetX =
            std::max(minOffsetX, std::min(maxOffsetX, m_TilePicker.targetOffsetX));
        m_TilePicker.targetOffsetY =
            std::max(minOffsetY, std::min(maxOffsetY, m_TilePicker.targetOffsetY));
    }

    // Toggles navigation map editing. When active:
    //   - Right-click toggles navigation flags on tiles
    //   - NPC placement mode is disabled (mutually exclusive)
    //   - Cyan overlay shows navigable tiles in debug view
    //
    // Navigation tiles determine where NPCs can walk for pathfinding.
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_M) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_M])
    {
        const bool enabling = m_EditMode != EditMode::Navigation;
        ClearAllEditModes();
        if (enabling)
            m_EditMode = EditMode::Navigation;
        std::cout << "Navigation edit mode: " << (enabling ? "ON" : "OFF") << std::endl;
        m_KeyPressed[GLFW_KEY_M] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_M) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_M] = false;
    }

    // Toggles NPC placement mode. When active:
    //   - Left-click places/removes NPCs on navigation tiles
    //   - Navigation edit mode is disabled (mutually exclusive)
    //   - Use , and . keys to cycle through available NPC types
    // N is claimed by NPC mode at the editor-wide scope. The particle-noProjection
    // override that wants N when ParticleZone is active is now bound to F so it
    // doesn't lose the race to this handler.
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_N) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_N])
    {
        const bool enabling = m_EditMode != EditMode::NPCPlacement;
        ClearAllEditModes();
        if (enabling)
        {
            m_EditMode = EditMode::NPCPlacement;
            ClampNPCTypeIndex();
            if (!m_AvailableNPCTypes.empty())
            {
                std::cout << "NPC placement mode: ON - Selected NPC: "
                          << m_AvailableNPCTypes[m_SelectedNPCTypeIndex] << std::endl;
                std::cout << "Press , (comma) and . (period) to cycle through NPC types"
                          << std::endl;
            }
        }
        else
        {
            std::cout << "NPC placement mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_N] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_N) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_N] = false;
    }

    // Toggles elevation editing mode. When active:
    //   - Left-click paints elevation values (for stairs)
    //   - Right-click removes elevation (sets to 0)
    //   - Use scroll to adjust elevation value
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_H) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_H])
    {
        const bool enabling = m_EditMode != EditMode::Elevation;
        ClearAllEditModes();
        if (enabling)
        {
            m_EditMode = EditMode::Elevation;
            std::cout << "Elevation edit mode: ON - Current elevation: " << m_CurrentElevation
                      << " pixels" << std::endl;
            std::cout << "Use scroll wheel to adjust elevation value" << std::endl;
        }
        else
        {
            std::cout << "Elevation edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_H] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_H) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_H] = false;
    }

    // Toggles no-projection editing mode. When active:
    //   - Left-click sets no-projection flag (tile renders without 3D effect)
    //   - Right-click clears no-projection flag
    //   - Used for buildings that should appear to have height in 3D mode
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_B) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_B])
    {
        const bool enabling = m_EditMode != EditMode::NoProjection;
        ClearAllEditModes();
        if (enabling)
        {
            m_EditMode = EditMode::NoProjection;
            std::cout << "No-projection edit mode: ON (Layer " << m_CurrentLayer
                      << ") - Click to mark tiles that bypass 3D projection" << std::endl;
            std::cout << "Use 1-6 keys to change layer" << std::endl;
        }
        else
        {
            std::cout << "No-projection edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_B] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_B) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_B] = false;
    }

    // Toggles Y-sort-plus editing mode. When active:
    //   - Left-click sets Y-sort-plus flag (tile sorts with entities by Y position)
    //   - Right-click clears Y-sort-plus flag
    //   - Used for tiles that should appear in front/behind player based on Y
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_Y) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_Y])
    {
        const bool enabling = m_EditMode != EditMode::YSortPlus;
        ClearAllEditModes();
        if (enabling)
        {
            m_EditMode = EditMode::YSortPlus;
            std::cout << "Y-sort+1 edit mode: ON (Layer " << m_CurrentLayer
                      << ") - Click to mark tiles for Y-sorting with entities" << std::endl;
            std::cout << "Use 1-6 keys to change layer" << std::endl;
        }
        else
        {
            std::cout << "Y-sort-plus edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_Y] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_Y) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_Y] = false;
    }

    // Toggles Y-sort-minus editing mode. When active:
    //   - Left-click sets Y-sort-minus flag (tile renders in front of player at same Y)
    //   - Right-click clears Y-sort-minus flag
    //   - Only affects tiles that are already Y-sort-plus
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_O) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_O])
    {
        const bool enabling = m_EditMode != EditMode::YSortMinus;
        ClearAllEditModes();
        if (enabling)
        {
            m_EditMode = EditMode::YSortMinus;
            std::cout << "========================================" << std::endl;
            std::cout << "Y-SORT-1 EDIT MODE: ON (Layer " << m_CurrentLayer << ")" << std::endl;
            std::cout << "Click the BOTTOM tile of a structure to mark it" << std::endl;
            std::cout << "(All tiles above will inherit the setting)" << std::endl;
            std::cout << "========================================" << std::endl;
        }
        else
        {
            std::cout << "Y-sort-minus edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_O] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_O) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_O] = false;
    }

    // Toggles particle zone editing mode. When active:
    //   - Left-click and drag to create a particle zone
    //   - Right-click to remove zone under cursor
    //   - Use , and . keys to cycle particle type
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_J) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_J])
    {
        const bool enabling = m_EditMode != EditMode::ParticleZone;
        ClearAllEditModes();
        if (enabling)
        {
            m_EditMode = EditMode::ParticleZone;
            std::cout << "Particle zone edit mode: ON - Type: "
                      << EnumTraits<ParticleType>::ToString(m_CurrentParticleType) << std::endl;
            std::cout << "Click and drag to place zones, use , and . to change type, F to toggle "
                         "noProjection override"
                      << std::endl;
        }
        else
        {
            std::cout << "Particle zone edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_J] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_J) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_J] = false;
    }

    // Particle type cycling
    if (m_Active && (m_EditMode == EditMode::ParticleZone))
    {
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_COMMA])
        {
            constexpr int N = static_cast<int>(EnumTraits<ParticleType>::Count);
            int type = (static_cast<int>(m_CurrentParticleType) + N - 1) % N;
            m_CurrentParticleType = static_cast<ParticleType>(type);
            std::cout << "Particle type: "
                      << EnumTraits<ParticleType>::ToString(m_CurrentParticleType) << std::endl;
            m_KeyPressed[GLFW_KEY_COMMA] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_COMMA] = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_PERIOD])
        {
            constexpr int N = static_cast<int>(EnumTraits<ParticleType>::Count);
            int type = (static_cast<int>(m_CurrentParticleType) + 1) % N;
            m_CurrentParticleType = static_cast<ParticleType>(type);
            std::cout << "Particle type: "
                      << EnumTraits<ParticleType>::ToString(m_CurrentParticleType) << std::endl;
            m_KeyPressed[GLFW_KEY_PERIOD] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_PERIOD] = false;

        // Toggles manual noProjection override for new particle zones.
        // Auto-detection from tiles is always active; this is for forcing
        // noProjection on/off. Bound to F ("Flat projection") because N is
        // already claimed globally by the NPC-placement mode toggle.
        if (glfwGetKey(ctx.window, GLFW_KEY_F) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_F])
        {
            m_ParticleNoProjection = !m_ParticleNoProjection;
            std::cout << "Particle noProjection override: "
                      << (m_ParticleNoProjection ? "ON (forced)" : "OFF (auto-detect)")
                      << std::endl;
            m_KeyPressed[GLFW_KEY_F] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_F) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_F] = false;
    }

    // Toggles structure definition mode. When active:
    //   - Click to place left anchor, click again to place right anchor
    //   - Enter to create structure from anchors
    //   - , and . to cycle through existing structures
    //   - Shift+click to assign tiles to current structure
    //   - Right-click to clear structure assignment from tiles
    //   - Delete to remove current structure
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_G) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_G])
    {
        const bool enabling = m_EditMode != EditMode::Structure;
        ClearAllEditModes();  // resets anchors, anchor-step, and flood flag
        if (enabling)
        {
            m_EditMode = EditMode::Structure;
            std::cout << "========================================" << std::endl;
            std::cout << "STRUCTURE EDIT MODE: ON (Layer " << (m_CurrentLayer + 1) << ")"
                      << std::endl;
            std::cout << "Click = toggle no-projection" << std::endl;
            std::cout << "Shift+click = flood-fill no-projection" << std::endl;
            std::cout << "Ctrl+click = place anchors (left, then right)" << std::endl;
            std::cout << ", . = select existing structures" << std::endl;
            std::cout << "Delete = remove selected structure" << std::endl;
            std::cout << "Structures: " << ctx.tilemap.GetNoProjectionStructureCount() << std::endl;
            std::cout << "========================================" << std::endl;
        }
        else
        {
            std::cout << "Structure edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_G] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_G) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_G] = false;
    }

    // Structure mode controls
    if (m_Active && (m_EditMode == EditMode::Structure))
    {
        // Cycle through structures with , and .
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_COMMA])
        {
            size_t count = ctx.tilemap.GetNoProjectionStructureCount();
            if (count > 0)
            {
                if (m_CurrentStructureId < 0)
                    m_CurrentStructureId = static_cast<int>(count) - 1;
                else
                    m_CurrentStructureId = (m_CurrentStructureId - 1 + static_cast<int>(count)) %
                                           static_cast<int>(count);

                const NoProjectionStructure* s =
                    ctx.tilemap.GetNoProjectionStructure(m_CurrentStructureId);
                if (s)
                {
                    std::cout << "Selected structure " << m_CurrentStructureId << ": \"" << s->name
                              << "\" anchors: (" << s->leftAnchor.x << "," << s->leftAnchor.y
                              << ") - (" << s->rightAnchor.x << "," << s->rightAnchor.y << ")"
                              << std::endl;
                }
            }
            m_KeyPressed[GLFW_KEY_COMMA] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_COMMA] = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_PERIOD])
        {
            size_t count = ctx.tilemap.GetNoProjectionStructureCount();
            if (count > 0)
            {
                m_CurrentStructureId = (m_CurrentStructureId + 1) % static_cast<int>(count);

                const NoProjectionStructure* s =
                    ctx.tilemap.GetNoProjectionStructure(m_CurrentStructureId);
                if (s)
                {
                    std::cout << "Selected structure " << m_CurrentStructureId << ": \"" << s->name
                              << "\" anchors: (" << s->leftAnchor.x << "," << s->leftAnchor.y
                              << ") - (" << s->rightAnchor.x << "," << s->rightAnchor.y << ")"
                              << std::endl;
                }
            }
            m_KeyPressed[GLFW_KEY_PERIOD] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_PERIOD] = false;

        // Escape to cancel anchor placement
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS &&
            !m_KeyPressed[GLFW_KEY_ESCAPE] && m_PlacingAnchor != 0)
        {
            m_PlacingAnchor = 0;
            m_TempLeftAnchor = glm::vec2(-1.0f, -1.0f);
            m_TempRightAnchor = glm::vec2(-1.0f, -1.0f);
            std::cout << "Anchor placement cancelled" << std::endl;
            m_KeyPressed[GLFW_KEY_ESCAPE] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_ESCAPE] = false;

        // Delete to remove current structure
        if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_DELETE])
        {
            if (m_CurrentStructureId >= 0)
            {
                std::cout << "Removed structure " << m_CurrentStructureId << std::endl;
                ctx.tilemap.RemoveNoProjectionStructure(m_CurrentStructureId);
                m_CurrentStructureId = -1;
            }
            m_KeyPressed[GLFW_KEY_DELETE] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_DELETE] = false;
    }

    // Toggles animated tile creation mode. When active:
    //   - Click tiles in the tile picker to add frames to animation
    //   - Press Enter to create the animation and apply to selected map tile
    //   - Press Escape to cancel/clear frames
    //   - Use , and . to adjust frame duration
    if (m_Active && glfwGetKey(ctx.window, GLFW_KEY_K) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_K])
    {
        const bool enabling = m_EditMode != EditMode::Animation;
        ClearAllEditModes();  // resets frame list and selected animation id
        if (enabling)
        {
            m_EditMode = EditMode::Animation;
            std::cout << "Animation edit mode: ON" << std::endl;
            std::cout << "Click tiles in picker to add frames, Enter to create, Esc to cancel"
                      << std::endl;
            std::cout << "Left-click map to apply animation, Right-click to remove animation"
                      << std::endl;
            std::cout << "Use , and . to adjust frame duration (current: "
                      << m_AnimationFrameDuration << "s)" << std::endl;
        }
        else
        {
            std::cout << "Animation edit mode: OFF" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_K] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_K) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_K] = false;
    }

    // Animation frame duration adjustment and controls
    if (m_Active && (m_EditMode == EditMode::Animation))
    {
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_COMMA])
        {
            m_AnimationFrameDuration = std::max(0.05f, m_AnimationFrameDuration - 0.05f);
            std::cout << "Animation frame duration: " << m_AnimationFrameDuration << "s"
                      << std::endl;
            m_KeyPressed[GLFW_KEY_COMMA] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_COMMA] = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_PERIOD])
        {
            m_AnimationFrameDuration = std::min(2.0f, m_AnimationFrameDuration + 0.05f);
            std::cout << "Animation frame duration: " << m_AnimationFrameDuration << "s"
                      << std::endl;
            m_KeyPressed[GLFW_KEY_PERIOD] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_PERIOD] = false;

        // Escape to clear frames and deselect animation
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_ESCAPE])
        {
            m_AnimationFrames.clear();
            m_SelectedAnimationId = -1;
            std::cout << "Animation frames/selection cleared" << std::endl;
            m_KeyPressed[GLFW_KEY_ESCAPE] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_ESCAPE] = false;

        // Enter to create animation
        if (glfwGetKey(ctx.window, GLFW_KEY_ENTER) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_ENTER])
        {
            if (m_AnimationFrames.size() >= 2)
            {
                AnimatedTile anim(m_AnimationFrames, m_AnimationFrameDuration);
                int animId = ctx.tilemap.AddAnimatedTile(anim);
                m_SelectedAnimationId = animId;
                std::cout << "Created animation #" << animId << " with " << m_AnimationFrames.size()
                          << " frames at " << m_AnimationFrameDuration << "s per frame"
                          << std::endl;
                std::cout << "Click on map tiles to apply this animation (Esc to cancel)"
                          << std::endl;
                m_AnimationFrames.clear();
                m_ShowTilePicker = false;  // Close tile picker to allow map clicking
            }
            else
            {
                std::cout << "Need at least 2 frames to create animation" << std::endl;
            }
            m_KeyPressed[GLFW_KEY_ENTER] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_ENTER) == GLFW_RELEASE)
            m_KeyPressed[GLFW_KEY_ENTER] = false;
    }

    // Cycles through available NPC types when in NPC placement mode.
    // Comma (,) previous type, Period (.) next type
    // Wraps around at list boundaries.
    ClampNPCTypeIndex();
    if (m_Active && (m_EditMode == EditMode::NPCPlacement) && !m_AvailableNPCTypes.empty())
    {
        // Comma key cycles to previous NPC type
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_COMMA])
        {
            if (m_SelectedNPCTypeIndex > 0)
            {
                m_SelectedNPCTypeIndex--;
            }
            else
            {
                m_SelectedNPCTypeIndex = m_AvailableNPCTypes.size() - 1;  // Wrap to end
            }
            std::cout << "Selected NPC type: " << m_AvailableNPCTypes[m_SelectedNPCTypeIndex]
                      << " (" << (m_SelectedNPCTypeIndex + 1) << "/" << m_AvailableNPCTypes.size()
                      << ")" << std::endl;
            m_KeyPressed[GLFW_KEY_COMMA] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
        {
            m_KeyPressed[GLFW_KEY_COMMA] = false;
        }

        // Period key cycles to next NPC type
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_PERIOD])
        {
            m_SelectedNPCTypeIndex =
                (m_SelectedNPCTypeIndex + 1) % m_AvailableNPCTypes.size();  // Wrap to start
            std::cout << "Selected NPC type: " << m_AvailableNPCTypes[m_SelectedNPCTypeIndex]
                      << " (" << (m_SelectedNPCTypeIndex + 1) << "/" << m_AvailableNPCTypes.size()
                      << ")" << std::endl;
            m_KeyPressed[GLFW_KEY_PERIOD] = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
        {
            m_KeyPressed[GLFW_KEY_PERIOD] = false;
        }
    }

    // Saves the current game to save.json including:
    //   - All tile layers with rotations
    //   - Collision map
    //   - Navigation map
    //   - NPC positions, dialogues and types
    //   - Player spawn position and character type
    if (glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_S] && m_Active)
    {
        // Calculate player's current tile for spawn point
        glm::vec2 playerPos = ctx.player.GetPosition();
        int playerTileX = static_cast<int>(std::floor(playerPos.x / ctx.tilemap.GetTileWidth()));
        int playerTileY =
            static_cast<int>(std::floor((playerPos.y - 0.1f) / ctx.tilemap.GetTileHeight()));
        int characterType = static_cast<int>(ctx.player.GetCharacterType());

        if (ctx.tilemap.SaveMapToJSON(
                "save.json", &ctx.npcs, playerTileX, playerTileY, characterType))
        {
            std::cout << "Save successful! Player at tile (" << playerTileX << ", " << playerTileY
                      << "), character type: " << characterType << std::endl;
            ShowStatus("Saved to save.json", glm::vec3(0.4f, 1.0f, 0.4f));
        }
        else
        {
            // Surfaces the failure. Previously the error only went to stderr,
            // so the user could keep editing a map they thought was saved.
            std::cerr << "Save FAILED to write save.json" << std::endl;
            ShowStatus("SAVE FAILED - check console", glm::vec3(1.0f, 0.3f, 0.3f), 5.0f);
        }
        m_KeyPressed[GLFW_KEY_S] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_S] = false;
    }

    // Reloads the game state from save.json, replacing all current state.
    // Also restores player position, character type, and recenters camera.
    if (glfwGetKey(ctx.window, GLFW_KEY_L) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_L] && m_Active)
    {
        int loadedPlayerTileX = -1;
        int loadedPlayerTileY = -1;
        int loadedCharacterType = -1;
        if (ctx.tilemap.LoadMapFromJSON("save.json",
                                        &ctx.npcs,
                                        &loadedPlayerTileX,
                                        &loadedPlayerTileY,
                                        &loadedCharacterType))
        {
            std::cout << "Save loaded successfully!" << std::endl;
            ShowStatus("Loaded save.json", glm::vec3(0.4f, 1.0f, 0.4f));

            // Restore character type if saved
            if (loadedCharacterType >= 0)
            {
                ctx.player.SwitchCharacter(static_cast<CharacterType>(loadedCharacterType));
                std::cout << "Player character restored to type " << loadedCharacterType
                          << std::endl;
            }

            // Restore player position if spawn point was saved
            if (loadedPlayerTileX >= 0 && loadedPlayerTileY >= 0)
            {
                ctx.player.SetTilePosition(loadedPlayerTileX, loadedPlayerTileY);

                // Recenter camera on player
                glm::vec2 playerPos = ctx.player.GetPosition();
                float camWorldWidth =
                    static_cast<float>(ctx.tilesVisibleWidth * ctx.tilemap.GetTileWidth());
                float camWorldHeight =
                    static_cast<float>(ctx.tilesVisibleHeight * ctx.tilemap.GetTileHeight());
                glm::vec2 playerVisualCenter = glm::vec2(playerPos.x, playerPos.y - 16.0f);
                ctx.cameraPosition =
                    playerVisualCenter - glm::vec2(camWorldWidth / 2.0f, camWorldHeight / 2.0f);
                ctx.cameraFollowTarget = ctx.cameraPosition;
                ctx.hasCameraFollowTarget = false;
                std::cout << "Player position restored to tile (" << loadedPlayerTileX << ", "
                          << loadedPlayerTileY << ")" << std::endl;
            }
        }
        else
        {
            std::cout << "Failed to reload map!" << std::endl;
            ShowStatus("LOAD FAILED - check console", glm::vec3(1.0f, 0.3f, 0.3f), 5.0f);
        }
        m_KeyPressed[GLFW_KEY_L] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_L) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_L] = false;
    }

    // Removes tiles under the mouse cursor on the currently selected layer.
    // Hold DEL and drag to delete multiple tiles continuously.
    if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_PRESS && m_Active && !m_ShowTilePicker)
    {
        double mouseX, mouseY;
        glfwGetCursorPos(ctx.window, &mouseX, &mouseY);
        auto st = ScreenToTileCoords(ctx, mouseX, mouseY);
        int tileX = st.tileX;
        int tileY = st.tileY;

        // Only delete if cursor moved to a new tile
        bool isNewTile = (tileX != m_LastDeletedTileX || tileY != m_LastDeletedTileY);

        // Bounds check before deletion
        if (isNewTile && tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
            tileY < ctx.tilemap.GetMapHeight())
        {
            // Delete tile on selected layer (set to -1 = empty) and clear animation
            ctx.tilemap.SetLayerTile(tileX, tileY, m_CurrentLayer, -1);
            ctx.tilemap.SetTileAnimation(tileX, tileY, static_cast<int>(m_CurrentLayer), -1);
            m_LastDeletedTileX = tileX;
            m_LastDeletedTileY = tileY;
        }
        m_KeyPressed[GLFW_KEY_DELETE] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_DELETE] = false;
        m_LastDeletedTileX = -1;
        m_LastDeletedTileY = -1;
    }

    // Rotates the tile under the mouse cursor by 90 on the current layer.
    // Note: This is different from multi-tile rotation which uses R when
    //       m_MultiTile.selectionMode is true.
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_PRESS && !m_KeyPressed[GLFW_KEY_R] && m_Active &&
        !m_ShowTilePicker)
    {
        double mouseX, mouseY;
        glfwGetCursorPos(ctx.window, &mouseX, &mouseY);
        auto st = ScreenToTileCoords(ctx, mouseX, mouseY);
        int tileX = st.tileX;
        int tileY = st.tileY;

        if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
            tileY < ctx.tilemap.GetMapHeight())
        {
            // Rotate tile by 90 degrees on selected layer
            float currentRotation = ctx.tilemap.GetLayerRotation(tileX, tileY, m_CurrentLayer);
            float newRotation = currentRotation + 90.0f;
            ctx.tilemap.SetLayerRotation(tileX, tileY, m_CurrentLayer, newRotation);
            std::cout << "Rotated Layer " << (m_CurrentLayer + 1) << " tile at (" << tileX << ", "
                      << tileY << ") to " << newRotation << " degrees" << std::endl;
        }
        m_KeyPressed[GLFW_KEY_R] = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_RELEASE)
    {
        m_KeyPressed[GLFW_KEY_R] = false;
    }

    // Selects which tile layer to edit.
    // Layer switching: Keys 1-9,0 map to dynamic layers 0-9
    static constexpr struct
    {
        int key;
        int layerIndex;
        const char* name;
    } kLayerKeys[] = {
        {GLFW_KEY_1, 0, "Layer 1: Ground (background)"},
        {GLFW_KEY_2, 1, "Layer 2: Ground Detail (background)"},
        {GLFW_KEY_3, 2, "Layer 3: Objects (background)"},
        {GLFW_KEY_4, 3, "Layer 4: Objects2 (background)"},
        {GLFW_KEY_5, 4, "Layer 5: Objects3 (background)"},
        {GLFW_KEY_6, 5, "Layer 6: Foreground (foreground)"},
        {GLFW_KEY_7, 6, "Layer 7: Foreground2 (foreground)"},
        {GLFW_KEY_8, 7, "Layer 8: Overlay (foreground)"},
        {GLFW_KEY_9, 8, "Layer 9: Overlay2 (foreground)"},
        {GLFW_KEY_0, 9, "Layer 10: Overlay3 (foreground)"},
    };
    for (const auto& [key, layerIndex, name] : kLayerKeys)
    {
        if (glfwGetKey(ctx.window, key) == GLFW_PRESS && !m_KeyPressed[key] && m_Active)
        {
            m_CurrentLayer = layerIndex;
            std::cout << "Switched to " << name << std::endl;
            m_KeyPressed[key] = true;
        }
        if (glfwGetKey(ctx.window, key) == GLFW_RELEASE)
            m_KeyPressed[key] = false;
    }
}

void Editor::ProcessMouseInput(const EditorContext& ctx)
{
    double mouseX, mouseY;
    glfwGetCursorPos(ctx.window, &mouseX, &mouseY);

    // Query mouse button states
    int leftMouseButton = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_LEFT);
    int rightMouseButton = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_RIGHT);
    bool leftMouseDown = (leftMouseButton == GLFW_PRESS);
    bool rightMouseDown = (rightMouseButton == GLFW_PRESS);

    // Right-click toggles collision or navigation flags depending on mode.
    // Supports drag-to-draw: first click sets target state, dragging applies it.
    if (rightMouseDown && !m_ShowTilePicker)
    {
        auto st = ScreenToTileCoords(ctx, mouseX, mouseY);
        float worldX = st.worldX;
        float worldY = st.worldY;
        int tileX = st.tileX;
        int tileY = st.tileY;

        // Check if cursor moved to a new tile
        bool isNewNavigationTilePosition =
            (tileX != m_Mouse.lastNavigationTileX || tileY != m_Mouse.lastNavigationTileY);
        bool isNewCollisionTilePosition =
            (tileX != m_Mouse.lastCollisionTileX || tileY != m_Mouse.lastCollisionTileY);

        if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
            tileY < ctx.tilemap.GetMapHeight())
        {
            // Animation edit mode, right-click removes animation from tile
            if ((m_EditMode == EditMode::Animation))
            {
                int currentAnim = ctx.tilemap.GetTileAnimation(tileX, tileY, m_CurrentLayer);
                if (currentAnim >= 0)
                {
                    ctx.tilemap.SetTileAnimation(tileX, tileY, m_CurrentLayer, -1);
                    std::cout << "Removed animation from tile (" << tileX << ", " << tileY
                              << ") on layer " << m_CurrentLayer << std::endl;
                }
                m_Mouse.rightMousePressed = true;
                return;
            }
            // Elevation edit mode, right-click clears elevation at tile
            else if ((m_EditMode == EditMode::Elevation))
            {
                ctx.tilemap.SetElevation(tileX, tileY, 0);
                std::cout << "Cleared elevation at (" << tileX << ", " << tileY << ")" << std::endl;
                m_Mouse.rightMousePressed = true;
            }
            // Structure edit mode, right-click clears structure assignment from tiles
            // Shift+right-click, flood-fill to clear all connected tiles
            else if ((m_EditMode == EditMode::Structure))
            {
                bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

                if (shiftHeld)
                {
                    int layer = m_CurrentLayer + 1;
                    int count = FloodFill(
                        ctx.tilemap,
                        tileX,
                        tileY,
                        [&](int cx, int cy)
                        { return ctx.tilemap.GetTileStructureId(cx, cy, layer) >= 0; },
                        [&](int cx, int cy) { ctx.tilemap.SetTileStructureId(cx, cy, layer, -1); });
                    std::cout << "Cleared structure assignment from " << count << " tiles (layer "
                              << layer << ")" << std::endl;
                }
                else
                {
                    // Single tile: clear structure assignment
                    ctx.tilemap.SetTileStructureId(tileX, tileY, m_CurrentLayer + 1, -1);
                    std::cout << "Cleared structure assignment at (" << tileX << ", " << tileY
                              << ")" << std::endl;
                }
                m_Mouse.rightMousePressed = true;
            }
            // No-projection edit mode, right-click clears no-projection flag for current layer
            // Shift+right-click, flood-fill to clear all connected tiles
            else if ((m_EditMode == EditMode::NoProjection))
            {
                bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

                if (shiftHeld)
                {
                    size_t layerCount = ctx.tilemap.GetLayerCount();
                    int count = FloodFill(
                        ctx.tilemap,
                        tileX,
                        tileY,
                        [&](int cx, int cy)
                        {
                            for (size_t li = 0; li < layerCount; ++li)
                                if (ctx.tilemap.GetLayerNoProjection(cx, cy, li))
                                    return true;
                            return false;
                        },
                        [&](int cx, int cy)
                        {
                            for (size_t li = 0; li < layerCount; ++li)
                                ctx.tilemap.SetLayerNoProjection(cx, cy, li, false);
                        });
                    std::cout << "Cleared no-projection on " << count
                              << " connected tiles (all layers)" << std::endl;
                }
                else
                {
                    // Clear noProjection on ALL layers at this position
                    for (size_t li = 0; li < ctx.tilemap.GetLayerCount(); ++li)
                    {
                        ctx.tilemap.SetLayerNoProjection(tileX, tileY, li, false);
                    }
                    std::cout << "Cleared no-projection at (" << tileX << ", " << tileY
                              << ") all layers" << std::endl;
                }
                m_Mouse.rightMousePressed = true;
            }
            // Y-sort-plus / Y-sort-minus edit modes share the same clear logic.
            // Right-click clears flag on single tile; Shift+right-click flood-clears.
            else if (m_EditMode == EditMode::YSortPlus || m_EditMode == EditMode::YSortMinus)
            {
                bool isPlus = (m_EditMode == EditMode::YSortPlus);
                auto getter = [&](int cx, int cy)
                {
                    return isPlus ? ctx.tilemap.GetLayerYSortPlus(cx, cy, m_CurrentLayer)
                                  : ctx.tilemap.GetLayerYSortMinus(cx, cy, m_CurrentLayer);
                };
                auto setter = [&](int cx, int cy)
                {
                    if (isPlus)
                    {
                        ctx.tilemap.SetLayerYSortPlus(cx, cy, m_CurrentLayer, false);
                    }
                    else
                    {
                        ctx.tilemap.SetLayerYSortMinus(cx, cy, m_CurrentLayer, false);
                    }
                };
                const char* label = isPlus ? "Y-sort-plus" : "Y-sort-minus";

                bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

                if (shiftHeld)
                {
                    int count = FloodFill(ctx.tilemap, tileX, tileY, getter, setter);
                    std::cout << "Cleared " << label << " on " << count
                              << " connected tiles (layer " << (m_CurrentLayer + 1) << ")"
                              << std::endl;
                }
                else
                {
                    setter(tileX, tileY);
                    std::cout << "Cleared " << label << " at (" << tileX << ", " << tileY
                              << ") layer " << (m_CurrentLayer + 1) << std::endl;
                }
                m_Mouse.rightMousePressed = true;
            }
            // Particle zone edit mode, right-click removes zone under cursor
            else if ((m_EditMode == EditMode::ParticleZone))
            {
                // Find zone under cursor and remove it
                auto* zones = ctx.tilemap.GetParticleZonesMutable();
                for (size_t i = 0; i < zones->size(); ++i)
                {
                    const ParticleZone& zone = (*zones)[i];
                    if (worldX >= zone.position.x && worldX < zone.position.x + zone.size.x &&
                        worldY >= zone.position.y && worldY < zone.position.y + zone.size.y)
                    {
                        std::cout << "Removed " << EnumTraits<ParticleType>::ToString(zone.type)
                                  << " zone at (" << zone.position.x << ", " << zone.position.y
                                  << ")" << std::endl;
                        ctx.particles.OnZoneRemoved(static_cast<int>(i));
                        ctx.tilemap.RemoveParticleZone(i);
                        break;
                    }
                }
                m_Mouse.rightMousePressed = true;
            }
            else if ((m_EditMode == EditMode::Navigation))
            {
                // Navigation editing mode, support drag-to-draw
                bool navigationChanged = false;
                if (!m_Mouse.rightMousePressed)
                {
                    // Initial click determines target state
                    bool walkable = ctx.tilemap.GetNavigation(tileX, tileY);
                    m_Mouse.navigationDragState = !walkable;  // Set to opposite of current state
                    ctx.tilemap.SetNavigation(tileX, tileY, m_Mouse.navigationDragState);
                    navigationChanged = true;
                    std::cout << "=== NAVIGATION DRAG START ===" << std::endl;
                    std::cout << "Tile (" << tileX << ", " << tileY
                              << "): " << (walkable ? "ON" : "OFF") << " -> "
                              << (m_Mouse.navigationDragState ? "ON" : "OFF") << std::endl;
                    m_Mouse.lastNavigationTileX = tileX;
                    m_Mouse.lastNavigationTileY = tileY;
                    m_Mouse.rightMousePressed = true;
                }
                else if (isNewNavigationTilePosition)
                {
                    // Dragging sets navigation to the same state as initial click
                    bool currentWalkable = ctx.tilemap.GetNavigation(tileX, tileY);
                    if (currentWalkable != m_Mouse.navigationDragState)
                    {
                        ctx.tilemap.SetNavigation(tileX, tileY, m_Mouse.navigationDragState);
                        navigationChanged = true;
                        std::cout << "Navigation drag: Tile (" << tileX << ", " << tileY << ") -> "
                                  << (m_Mouse.navigationDragState ? "ON" : "OFF") << std::endl;
                    }
                    m_Mouse.lastNavigationTileX = tileX;
                    m_Mouse.lastNavigationTileY = tileY;
                }

                // Recalculate patrol routes when navigation changes
                if (navigationChanged)
                {
                    RecalculateNPCPatrolRoutes(ctx);
                }
            }
            else
            {
                // Collision editing mode, support drag-to-draw
                if (!m_Mouse.rightMousePressed)
                {
                    // Initial click determines target state
                    bool currentCollision = ctx.tilemap.GetTileCollision(tileX, tileY);
                    m_Mouse.collisionDragState =
                        !currentCollision;  // Set to opposite of current state
                    ctx.tilemap.SetTileCollision(tileX, tileY, m_Mouse.collisionDragState);
                    std::cout << "=== COLLISION DRAG START ===" << std::endl;
                    std::cout << "Tile (" << tileX << ", " << tileY
                              << "): " << (currentCollision ? "ON" : "OFF") << " -> "
                              << (m_Mouse.collisionDragState ? "ON" : "OFF") << std::endl;
                    m_Mouse.lastCollisionTileX = tileX;
                    m_Mouse.lastCollisionTileY = tileY;
                    m_Mouse.rightMousePressed = true;
                }
                else if (isNewCollisionTilePosition)
                {
                    // Dragging sets collision to the same state as initial click
                    bool currentCollision = ctx.tilemap.GetTileCollision(tileX, tileY);
                    if (currentCollision != m_Mouse.collisionDragState)
                    {
                        ctx.tilemap.SetTileCollision(tileX, tileY, m_Mouse.collisionDragState);
                        std::cout << "Collision drag: Tile (" << tileX << ", " << tileY << ") -> "
                                  << (m_Mouse.collisionDragState ? "ON" : "OFF") << std::endl;
                    }
                    m_Mouse.lastCollisionTileX = tileX;
                    m_Mouse.lastCollisionTileY = tileY;
                }
            }
        }
        else
        {
            if (!m_Mouse.rightMousePressed)
            {
                std::cout << "Right-click outside map bounds (tileX=" << tileX << " tileY=" << tileY
                          << " map size=" << ctx.tilemap.GetMapWidth() << "x"
                          << ctx.tilemap.GetMapHeight() << ")" << std::endl;
            }
        }
    }
    else if (!rightMouseDown)
    {
        m_Mouse.rightMousePressed = false;
        // Reset navigation and collision drag tracking when mouse is released
        m_Mouse.lastNavigationTileX = -1;
        m_Mouse.lastNavigationTileY = -1;
        m_Mouse.lastCollisionTileX = -1;
        m_Mouse.lastCollisionTileY = -1;
    }

    // Handle tile picker selection
    if (m_ShowTilePicker)
    {
        int dataTilesPerRow = ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();
        int dataTilesPerCol = ctx.tilemap.GetTilesetDataHeight() / ctx.tilemap.GetTileHeight();
        int totalTiles = dataTilesPerRow * dataTilesPerCol;
        int tilesPerRow = dataTilesPerRow;
        float baseTileSize =
            (static_cast<float>(ctx.screenWidth) / static_cast<float>(tilesPerRow)) * 1.5f;
        float tileSize = baseTileSize * m_TilePicker.zoom;

        // Start selection on mouse down
        if (leftMouseDown && !m_Mouse.mousePressed && !m_MultiTile.isSelecting)
        {
            if (mouseX >= 0 && mouseX < ctx.screenWidth && mouseY >= 0 && mouseY < ctx.screenHeight)
            {
                // Account for offset when calculating tile position
                double adjustedMouseX = mouseX - m_TilePicker.offsetX;
                double adjustedMouseY = mouseY - m_TilePicker.offsetY;
                int pickerTileX = static_cast<int>(adjustedMouseX / tileSize);
                int pickerTileY = static_cast<int>(adjustedMouseY / tileSize);
                int localIndex = pickerTileY * tilesPerRow + pickerTileX;
                int clickedTileID = localIndex;

                if (clickedTileID >= 0 && clickedTileID < totalTiles)
                {
                    // Animation edit mode, collect frames instead of normal selection
                    if ((m_EditMode == EditMode::Animation))
                    {
                        // Add frame to animation
                        m_AnimationFrames.push_back(clickedTileID);
                        m_Mouse.mousePressed = true;
                        std::cout << "Added animation frame: " << clickedTileID
                                  << " (total frames: " << m_AnimationFrames.size() << ")"
                                  << std::endl;
                    }
                    else
                    {
                        m_MultiTile.isSelecting = true;
                        m_MultiTile.selectionStartTileID = clickedTileID;
                        m_SelectedTileID = clickedTileID;
                        m_Mouse.mousePressed = true;  // Prevent other click handlers from firing
                        std::cout << "Started selection at tile ID: " << clickedTileID
                                  << " (mouse: " << mouseX << ", " << mouseY
                                  << ", adjusted: " << adjustedMouseX << ", " << adjustedMouseY
                                  << ", offset: " << m_TilePicker.offsetX << ", "
                                  << m_TilePicker.offsetY << ")" << std::endl;
                    }
                }
            }
        }

        // Update selection while dragging
        if (leftMouseDown && m_MultiTile.isSelecting)
        {
            if (mouseX >= 0 && mouseX < ctx.screenWidth && mouseY >= 0 && mouseY < ctx.screenHeight)
            {
                // Account for offset when calculating tile position
                double adjustedMouseX = mouseX - m_TilePicker.offsetX;
                double adjustedMouseY = mouseY - m_TilePicker.offsetY;
                int pickerTileX = static_cast<int>(adjustedMouseX / tileSize);
                int pickerTileY = static_cast<int>(adjustedMouseY / tileSize);
                int localIndex = pickerTileY * tilesPerRow + pickerTileX;
                int clickedTileID = localIndex;

                if (clickedTileID >= 0 && clickedTileID < totalTiles)
                {
                    m_SelectedTileID = clickedTileID;
                }
            }
        }

        // Reset mouse pressed state when mouse released in animation mode
        if (!leftMouseDown && (m_EditMode == EditMode::Animation) && m_Mouse.mousePressed)
        {
            m_Mouse.mousePressed = false;
        }

        // Finish selection on mouse up
        if (!leftMouseDown && m_MultiTile.isSelecting)
        {
            if (m_MultiTile.selectionStartTileID >= 0)
            {
                int startTileID = m_MultiTile.selectionStartTileID;
                int endTileID = m_SelectedTileID;

                int startX = startTileID % dataTilesPerRow;
                int startY = startTileID / dataTilesPerRow;
                int endX = endTileID % dataTilesPerRow;
                int endY = endTileID / dataTilesPerRow;

                int minX = std::min(startX, endX);
                int maxX = std::max(startX, endX);
                int minY = std::min(startY, endY);
                int maxY = std::max(startY, endY);

                m_MultiTile.selectedStartID = minY * dataTilesPerRow + minX;
                m_MultiTile.width = maxX - minX + 1;
                m_MultiTile.height = maxY - minY + 1;

                if (m_MultiTile.width > 1 || m_MultiTile.height > 1)
                {
                    // Multi-tile selection, enable placement mode,
                    // but do not change the world camera or zoom.
                    m_MultiTile.selectionMode = true;
                    m_MultiTile.isPlacing = true;
                    m_MultiTile.rotation = 0;  // Reset rotation for new selection
                    std::cout << "=== MULTI-TILE SELECTION ===" << std::endl;
                    std::cout << "Start tile ID: " << m_MultiTile.selectedStartID << std::endl;
                    std::cout << "Size: " << m_MultiTile.width << "x" << m_MultiTile.height
                              << std::endl;
                }
                else
                {
                    m_MultiTile.selectionMode = false;
                    m_MultiTile.isPlacing = false;
                    m_MultiTile.rotation = 0;  // Reset rotation
                    std::cout << "=== SINGLE TILE SELECTION ===" << std::endl;
                    std::cout << "Tile ID: " << m_MultiTile.selectedStartID << std::endl;
                }

                m_ShowTilePicker = false;
            }
            m_MultiTile.isSelecting = false;
            m_MultiTile.selectionStartTileID = -1;
            m_Mouse.mousePressed = false;  // Reset mouse pressed state
        }

        // Early return to prevent tile placement when tile picker is shown
        if (m_ShowTilePicker)
        {
            // Update mouse position for preview
            m_Mouse.lastMouseX = mouseX;
            m_Mouse.lastMouseY = mouseY;
            return;  // Don't process tile placement when picker is shown
        }
    }

    // Handle left mouse click
    if (leftMouseDown && !m_ShowTilePicker)
    {
        auto st = ScreenToTileCoords(ctx, mouseX, mouseY);
        float worldX = st.worldX;
        float worldY = st.worldY;
        int tileX = st.tileX;
        int tileY = st.tileY;

        // NPC placement mode, toggle NPC on this tile instead of placing tiles
        if (m_Active && (m_EditMode == EditMode::NPCPlacement))
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                // Only process if this is a new tile
                if (tileX == m_Mouse.lastNPCPlacementTileX &&
                    tileY == m_Mouse.lastNPCPlacementTileY)
                {
                    return;  // Already processed this tile during this click
                }
                m_Mouse.lastNPCPlacementTileX = tileX;
                m_Mouse.lastNPCPlacementTileY = tileY;

                const int tileSize = ctx.tilemap.GetTileWidth();

                // First, try to remove any NPC at this tile (works on any tile)
                bool removed = false;
                for (auto it = ctx.npcs.begin(); it != ctx.npcs.end(); ++it)
                {
                    if (it->GetTileX() == tileX && it->GetTileY() == tileY)
                    {
                        ctx.npcs.erase(it);
                        removed = true;
                        std::cout << "Removed NPC at tile (" << tileX << ", " << tileY << ")"
                                  << std::endl;
                        break;
                    }
                }

                // Only place new NPCs on navigation tiles
                if (!removed && ctx.tilemap.GetNavigation(tileX, tileY))
                {
                    ClampNPCTypeIndex();
                    if (!m_AvailableNPCTypes.empty())
                    {
                        NonPlayerCharacter npc;
                        std::string npcType = m_AvailableNPCTypes[m_SelectedNPCTypeIndex];
                        if (npc.Load(npcType))
                        {
                            npc.SetTilePosition(tileX, tileY, tileSize);

                            // Randomly assign a dialogue tree from the pool
                            // TODO: Load from save.json only and create dialogues via editor
                            DialogueTree tree;
                            std::string npcName;
                            static std::mt19937 rng(std::random_device{}());
                            // Total dialogue types: mystery dialogues + editor-aware + annoyed NPC
                            const int TOTAL_DIALOGUE_TYPES = kMysteryDialogueCount + 2;
                            std::uniform_int_distribution<int> dist(0, TOTAL_DIALOGUE_TYPES - 1);
                            int dialogueType = dist(rng);
                            if (dialogueType < kMysteryDialogueCount)
                            {
                                BuildMysteryDialogueTree(
                                    tree, npcName, kMysteryDialogues[dialogueType]);
                            }
                            else if (dialogueType == kMysteryDialogueCount)
                            {
                                BuildEditorAwareDialogueTree(tree, npcName);
                            }
                            else
                            {
                                BuildAnnoyedNPCDialogueTree(tree, npcName);
                            }

                            npc.SetDialogueTree(tree);
                            npc.SetName(npcName);

                            ctx.npcs.emplace_back(std::move(npc));
                            std::cout << "Placed NPC " << npcType << " at tile (" << tileX << ", "
                                      << tileY << ") with dialogue tree" << std::endl;
                        }
                        else
                        {
                            std::cerr << "Failed to load NPC type: " << npcType << std::endl;
                        }
                    }
                    else
                    {
                        std::cerr << "No NPC types available!" << std::endl;
                    }
                }
            }
            // In NPC placement mode we don't place tiles
            return;
        }

        // Particle zone editing mode, click and drag to create zones
        if (m_Active && (m_EditMode == EditMode::ParticleZone))
        {
            if (!m_PlacingParticleZone)
            {
                // Start placing a new zone
                m_PlacingParticleZone = true;
                // Snap to tile grid
                m_ParticleZoneStart.x = static_cast<float>(tileX * ctx.tilemap.GetTileWidth());
                m_ParticleZoneStart.y = static_cast<float>(tileY * ctx.tilemap.GetTileHeight());
            }
            // Zone is created on mouse release, so just track mouse here
            return;
        }

        // Animation edit mode, apply selected animation to clicked tile
        if (m_Active && (m_EditMode == EditMode::Animation) && m_SelectedAnimationId >= 0)
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                ctx.tilemap.SetTileAnimation(tileX, tileY, m_CurrentLayer, m_SelectedAnimationId);
                std::cout << "Applied animation #" << m_SelectedAnimationId << " to tile (" << tileX
                          << ", " << tileY << ") layer " << m_CurrentLayer << std::endl;
            }
            return;
        }

        // Elevation editing mode, paint elevation values
        if (m_Active && (m_EditMode == EditMode::Elevation))
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                ctx.tilemap.SetElevation(tileX, tileY, m_CurrentElevation);
                std::cout << "Set elevation at (" << tileX << ", " << tileY << ") to "
                          << m_CurrentElevation << std::endl;
            }
            return;
        }

        // Structure editing mode - works like no-projection mode with anchor placement
        // Click = toggle no-projection, Shift+click = flood-fill, Ctrl+click = place anchors
        if (m_Active && (m_EditMode == EditMode::Structure))
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
                bool ctrlHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                 glfwGetKey(ctx.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

                if (ctrlHeld && !m_Mouse.mousePressed)
                {
                    // Ctrl+click: place anchor at clicked corner of tile (no tile modification)
                    int tileWidth = ctx.tilemap.GetTileWidth();
                    int tileHeight = ctx.tilemap.GetTileHeight();
                    float tileCenterX = (tileX + 0.5f) * tileWidth;
                    float tileCenterY = (tileY + 0.5f) * tileHeight;

                    bool clickedRight = worldX >= tileCenterX;
                    bool clickedBottom = worldY >= tileCenterY;

                    float cornerX = static_cast<float>(clickedRight ? (tileX + 1) * tileWidth
                                                                    : tileX * tileWidth);
                    float cornerY = static_cast<float>(clickedBottom ? (tileY + 1) * tileHeight
                                                                     : tileY * tileHeight);

                    const char* cornerNames[4] = {
                        "top-left", "top-right", "bottom-left", "bottom-right"};
                    int cornerIdx = (clickedBottom ? 2 : 0) + (clickedRight ? 1 : 0);

                    if (m_PlacingAnchor == 0 || m_PlacingAnchor == 1)
                    {
                        // Place left anchor
                        m_TempLeftAnchor = glm::vec2(cornerX, cornerY);
                        m_PlacingAnchor = 2;
                        m_Mouse.mousePressed = true;
                        std::cout << "Left anchor: " << cornerNames[cornerIdx] << " of tile ("
                                  << tileX << ", " << tileY << ")" << std::endl;
                    }
                    else if (m_PlacingAnchor == 2)
                    {
                        // Place right anchor and create structure
                        m_TempRightAnchor = glm::vec2(cornerX, cornerY);
                        m_PlacingAnchor = 0;
                        m_Mouse.mousePressed = true;

                        int id = ctx.tilemap.AddNoProjectionStructure(m_TempLeftAnchor,
                                                                      m_TempRightAnchor);
                        m_CurrentStructureId = id;
                        std::cout << "Right anchor: " << cornerNames[cornerIdx] << " of tile ("
                                  << tileX << ", " << tileY << ")" << std::endl;
                        std::cout << "Created structure " << id << std::endl;
                        m_TempLeftAnchor = glm::vec2(-1.0f, -1.0f);
                        m_TempRightAnchor = glm::vec2(-1.0f, -1.0f);
                    }
                    // Don't process any tile modifications when placing anchors
                }
                else if (shiftHeld && !m_Mouse.mousePressed)
                {
                    // Shift+click: flood-fill set no-projection and assign to structure
                    m_Mouse.mousePressed = true;
                    int layer = m_CurrentLayer;
                    int structId = m_CurrentStructureId;
                    int count = FloodFill(
                        ctx.tilemap,
                        tileX,
                        tileY,
                        [&](int cx, int cy)
                        {
                            return ctx.tilemap.GetLayerTile(cx, cy, layer) >= 0 ||
                                   ctx.tilemap.GetTileAnimation(cx, cy, layer) >= 0;
                        },
                        [&](int cx, int cy)
                        {
                            ctx.tilemap.SetLayerNoProjection(cx, cy, layer, true);
                            if (structId >= 0)
                                ctx.tilemap.SetTileStructureId(cx, cy, layer + 1, structId);
                        });
                    if (structId >= 0)
                        std::cout << "Set no-projection on " << count
                                  << " tiles, assigned to structure " << structId << std::endl;
                    else
                        std::cout << "Set no-projection on " << count << " tiles (no structure)"
                                  << std::endl;
                }
                else if (!ctrlHeld && !shiftHeld && !m_Mouse.mousePressed)
                {
                    // Normal click: toggle no-projection on single tile
                    m_Mouse.mousePressed = true;
                    bool current = ctx.tilemap.GetLayerNoProjection(tileX, tileY, m_CurrentLayer);
                    ctx.tilemap.SetLayerNoProjection(tileX, tileY, m_CurrentLayer, !current);
                    if (m_CurrentStructureId >= 0 && !current)
                    {
                        ctx.tilemap.SetTileStructureId(
                            tileX, tileY, m_CurrentLayer + 1, m_CurrentStructureId);
                    }
                    std::cout << (current ? "Cleared" : "Set") << " no-projection at (" << tileX
                              << ", " << tileY << ")" << std::endl;
                }
            }
            return;
        }

        // No-projection editing mode, set no-projection flag for current layer
        // Shift+click, flood-fill to mark all connected tiles in the shape
        if (m_Active && (m_EditMode == EditMode::NoProjection))
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

                if (shiftHeld)
                {
                    int layer = m_CurrentLayer;
                    int count = FloodFill(
                        ctx.tilemap,
                        tileX,
                        tileY,
                        [&](int cx, int cy)
                        {
                            return ctx.tilemap.GetLayerTile(cx, cy, layer) >= 0 ||
                                   ctx.tilemap.GetTileAnimation(cx, cy, layer) >= 0;
                        },
                        [&](int cx, int cy)
                        { ctx.tilemap.SetLayerNoProjection(cx, cy, layer, true); });
                    std::cout << "Set no-projection on " << count << " connected tiles (layer "
                              << (layer + 1) << ")" << std::endl;
                }
                else
                {
                    // Single tile: set noProjection on current layer only
                    ctx.tilemap.SetLayerNoProjection(tileX, tileY, m_CurrentLayer, true);
                    std::cout << "Set no-projection at (" << tileX << ", " << tileY << ") on layer "
                              << (m_CurrentLayer + 1) << std::endl;
                }
            }
            return;
        }

        // Y-sort-plus editing mode, set Y-sort-plus flag for current layer
        // Shift+click, flood-fill to mark all connected tiles in the shape
        if (m_Active && (m_EditMode == EditMode::YSortPlus))
        {
            SetLayerFlagAtTile(ctx, tileX, tileY, &Tilemap::SetLayerYSortPlus, "Y-sort-plus");
            return;
        }

        // Y-sort-minus editing mode, set Y-sort-minus flag for current layer
        // Shift+click, flood-fill to mark all connected tiles in the shape
        if (m_Active && (m_EditMode == EditMode::YSortMinus))
        {
            SetLayerFlagAtTile(ctx, tileX, tileY, &Tilemap::SetLayerYSortMinus, "Y-sort-minus");
            // Warn if Y-sort-plus isn't set on this tile (only relevant for single-tile placement)
            bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                              glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
            if (!shiftHeld && tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                bool isYSortPlus = ctx.tilemap.GetLayerYSortPlus(tileX, tileY, m_CurrentLayer);
                if (!isYSortPlus)
                    std::cout << "  Warning: tile must also be Y-sort-plus!" << std::endl;
            }
            return;
        }

        // Check if this is a new tile position
        bool isNewTilePosition =
            (tileX != m_Mouse.lastPlacedTileX || tileY != m_Mouse.lastPlacedTileY);

        if (m_MultiTile.selectionMode)
        {
            // Multi-tile placement, only place on initial click, not on drag
            if (!m_Mouse.mousePressed)
            {
                int dataTilesPerRow =
                    ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();

                int rotatedWidth = (m_MultiTile.rotation == 90 || m_MultiTile.rotation == 270)
                                       ? m_MultiTile.height
                                       : m_MultiTile.width;
                int rotatedHeight = (m_MultiTile.rotation == 90 || m_MultiTile.rotation == 270)
                                        ? m_MultiTile.width
                                        : m_MultiTile.height;
                float tileRotation = GetCompensatedTileRotation();

                for (int dy = 0; dy < rotatedHeight; ++dy)
                {
                    for (int dx = 0; dx < rotatedWidth; ++dx)
                    {
                        int sourceDx, sourceDy;
                        CalculateRotatedSourceTile(dx, dy, sourceDx, sourceDy);

                        int placeX = tileX + dx;
                        int placeY = tileY + dy;
                        int sourceTileID =
                            m_MultiTile.selectedStartID + sourceDy * dataTilesPerRow + sourceDx;

                        if (placeX >= 0 && placeX < ctx.tilemap.GetMapWidth() && placeY >= 0 &&
                            placeY < ctx.tilemap.GetMapHeight())
                        {
                            ctx.tilemap.SetLayerTile(placeX, placeY, m_CurrentLayer, sourceTileID);
                            ctx.tilemap.SetLayerRotation(
                                placeX, placeY, m_CurrentLayer, tileRotation);
                        }
                    }
                }
                std::cout << "Placed " << m_MultiTile.width << "x" << m_MultiTile.height
                          << " tiles starting at (" << tileX << ", " << tileY << ") on layer "
                          << (m_CurrentLayer + 1) << std::endl;

                // Keep multi-tile selection active for multiple placements
                m_Mouse.lastPlacedTileX = tileX;
                m_Mouse.lastPlacedTileY = tileY;
                m_Mouse.mousePressed = true;
            }
        }
        else
        {
            // Single tile placement, support drag-to-place with rotation
            if (isNewTilePosition || !m_Mouse.mousePressed)
            {
                if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                    tileY < ctx.tilemap.GetMapHeight())
                {
                    float tileRotation = GetCompensatedTileRotation();
                    ctx.tilemap.SetLayerTile(
                        tileX, tileY, m_CurrentLayer, m_MultiTile.selectedStartID);
                    ctx.tilemap.SetLayerRotation(tileX, tileY, m_CurrentLayer, tileRotation);

                    m_Mouse.lastPlacedTileX = tileX;
                    m_Mouse.lastPlacedTileY = tileY;
                    m_Mouse.mousePressed = true;
                }
            }
        }
    }

    // Reset mouse pressed state and last placed tile position when mouse button is released
    if (!leftMouseDown)
    {
        // Finalize particle zone placement on mouse release
        if (m_PlacingParticleZone && (m_EditMode == EditMode::ParticleZone))
        {
            auto st = ScreenToTileCoords(ctx, mouseX, mouseY);
            auto zr = CalculateParticleZoneRect(
                st.worldX, st.worldY, ctx.tilemap.GetTileWidth(), ctx.tilemap.GetTileHeight());

            // Create the zone
            ParticleZone zone;
            zone.position = glm::vec2(zr.x, zr.y);
            zone.size = glm::vec2(zr.w, zr.h);
            zone.type = m_CurrentParticleType;
            zone.enabled = true;

            // Auto-detect noProjection from tiles
            int tw = ctx.tilemap.GetTileWidth();
            int th = ctx.tilemap.GetTileHeight();
            int minTileX = static_cast<int>(zr.x / tw);
            int minTileY = static_cast<int>(zr.y / th);
            int maxTileX = minTileX + static_cast<int>(zr.w / tw) - 1;
            int maxTileY = minTileY + static_cast<int>(zr.h / th) - 1;

            bool hasNoProjection = m_ParticleNoProjection;  // Start with manual setting
            if (!hasNoProjection)
            {
                // Check all tiles in the zone across all layers
                for (int ty = minTileY; ty <= maxTileY && !hasNoProjection; ty++)
                {
                    for (int tx = minTileX; tx <= maxTileX && !hasNoProjection; tx++)
                    {
                        for (size_t layer = 0; layer < ctx.tilemap.GetLayerCount(); layer++)
                        {
                            if (ctx.tilemap.GetLayerNoProjection(tx, ty, layer))
                            {
                                hasNoProjection = true;
                                break;
                            }
                        }
                    }
                }
            }
            zone.noProjection = hasNoProjection;
            ctx.tilemap.AddParticleZone(zone);

            std::cout << "Created " << EnumTraits<ParticleType>::ToString(m_CurrentParticleType)
                      << " zone at (" << zr.x << ", " << zr.y << ") size " << zr.w << "x" << zr.h;
            if (hasNoProjection)
                std::cout << " [noProjection]";
            std::cout << std::endl;

            m_PlacingParticleZone = false;
        }

        m_Mouse.mousePressed = false;
        m_Mouse.lastPlacedTileX = -1;
        m_Mouse.lastPlacedTileY = -1;
        m_Mouse.lastNPCPlacementTileX = -1;
        m_Mouse.lastNPCPlacementTileY = -1;
    }

    // Update mouse position for preview
    m_Mouse.lastMouseX = mouseX;
    m_Mouse.lastMouseY = mouseY;
}

void Editor::HandleScroll(double yoffset, const EditorContext& ctx)
{
    // Check for Ctrl modifier
    int ctrlState = glfwGetKey(ctx.window, GLFW_KEY_LEFT_CONTROL) |
                    glfwGetKey(ctx.window, GLFW_KEY_RIGHT_CONTROL);

    // Elevation adjustment with scroll wheel when in elevation edit mode
    if ((m_EditMode == EditMode::Elevation) && ctrlState != GLFW_PRESS)
    {
        if (yoffset > 0)
        {
            m_CurrentElevation += 2;
            if (m_CurrentElevation > 32)
                m_CurrentElevation = 32;
        }
        else if (yoffset < 0)
        {
            m_CurrentElevation -= 2;
            if (m_CurrentElevation < -32)
                m_CurrentElevation = -32;
        }
        std::cout << "Elevation value: " << m_CurrentElevation << " pixels" << std::endl;
        return;
    }

    // Tile picker scroll/zoom
    if (m_ShowTilePicker)
    {
        int dataTilesPerRow = ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();
        int dataTilesPerCol = ctx.tilemap.GetTilesetDataHeight() / ctx.tilemap.GetTileHeight();
        float baseTileSizePixels =
            (static_cast<float>(ctx.screenWidth) / static_cast<float>(dataTilesPerRow)) * 1.5f;

        if (ctrlState == GLFW_PRESS)
        {
            // Zoom centered on mouse
            double mouseX, mouseY;
            glfwGetCursorPos(ctx.window, &mouseX, &mouseY);

            float oldTileSize = baseTileSizePixels * m_TilePicker.zoom;

            float adjustedMouseX = static_cast<float>(mouseX) - m_TilePicker.offsetX;
            float adjustedMouseY = static_cast<float>(mouseY) - m_TilePicker.offsetY;
            int pickerTileX = static_cast<int>(adjustedMouseX / oldTileSize);
            int pickerTileY = static_cast<int>(adjustedMouseY / oldTileSize);

            float zoomDelta = yoffset > 0 ? 1.1f : 0.9f;
            m_TilePicker.zoom *= zoomDelta;
            m_TilePicker.zoom = std::max(0.25f, std::min(8.0f, m_TilePicker.zoom));

            float newTileSize = baseTileSizePixels * m_TilePicker.zoom;

            // Keep the tile under the cursor fixed by adjusting offsets
            float newTileCenterX = pickerTileX * newTileSize + newTileSize * 0.5f;
            float newTileCenterY = pickerTileY * newTileSize + newTileSize * 0.5f;
            float newOffsetX = static_cast<float>(mouseX) - newTileCenterX;
            float newOffsetY = static_cast<float>(mouseY) - newTileCenterY;

            // Clamp offsets so the sheet stays within viewable bounds
            float totalTilesWidth = newTileSize * dataTilesPerRow;
            float totalTilesHeight = newTileSize * dataTilesPerCol;
            float minOffsetX = ctx.screenWidth - totalTilesWidth;
            float maxOffsetX = 0.0f;
            float minOffsetY = ctx.screenHeight - totalTilesHeight;
            float maxOffsetY = 0.0f;

            newOffsetX = std::max(minOffsetX, std::min(maxOffsetX, newOffsetX));
            newOffsetY = std::max(minOffsetY, std::min(maxOffsetY, newOffsetY));

            // For zoom, update both current and target for immediate response
            m_TilePicker.offsetX = newOffsetX;
            m_TilePicker.offsetY = newOffsetY;
            m_TilePicker.targetOffsetX = newOffsetX;
            m_TilePicker.targetOffsetY = newOffsetY;

            std::cout << "Tile picker zoom: " << m_TilePicker.zoom
                      << "x (offset: " << m_TilePicker.offsetX << ", " << m_TilePicker.offsetY
                      << ")" << std::endl;
        }
        else
        {
            // Vertical pan with scroll wheel
            float panAmount = static_cast<float>(yoffset) * 200.0f;
            m_TilePicker.targetOffsetY += panAmount;

            float tileSizePixels = baseTileSizePixels * m_TilePicker.zoom;
            float totalTilesWidth = tileSizePixels * dataTilesPerRow;
            float totalTilesHeight = tileSizePixels * dataTilesPerCol;
            float minOffsetX = ctx.screenWidth - totalTilesWidth;
            float maxOffsetX = 0.0f;
            float minOffsetY = ctx.screenHeight - totalTilesHeight;
            float maxOffsetY = 0.0f;

            m_TilePicker.targetOffsetX =
                std::max(minOffsetX, std::min(maxOffsetX, m_TilePicker.targetOffsetX));
            m_TilePicker.targetOffsetY =
                std::max(minOffsetY, std::min(maxOffsetY, m_TilePicker.targetOffsetY));
        }
    }
}

void Editor::CalculateRotatedSourceTile(int dx, int dy, int& sourceDx, int& sourceDy) const
{
    if (m_MultiTile.rotation == 0)
    {
        sourceDx = dx;
        sourceDy = dy;
    }
    else if (m_MultiTile.rotation == 90)
    {
        sourceDx = m_MultiTile.width - 1 - dy;
        sourceDy = dx;
    }
    else if (m_MultiTile.rotation == 180)
    {
        sourceDx = m_MultiTile.width - 1 - dx;
        sourceDy = m_MultiTile.height - 1 - dy;
    }
    else  // 270 degrees
    {
        sourceDx = dy;
        sourceDy = m_MultiTile.height - 1 - dx;
    }
}

float Editor::GetCompensatedTileRotation() const
{
    float tileRotation = static_cast<float>(m_MultiTile.rotation);
    if (m_MultiTile.rotation == 90 || m_MultiTile.rotation == 270)
        tileRotation = static_cast<float>((m_MultiTile.rotation + 180) % 360);
    return tileRotation;
}

void Editor::SetLayerFlagAtTile(const EditorContext& ctx,
                                int tileX,
                                int tileY,
                                void (Tilemap::*setter)(int, int, size_t, bool),
                                const std::string& flagName)
{
    if (tileX < 0 || tileX >= ctx.tilemap.GetMapWidth() || tileY < 0 ||
        tileY >= ctx.tilemap.GetMapHeight())
        return;

    bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                      glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

    if (shiftHeld)
    {
        int layer = m_CurrentLayer;
        int count = FloodFill(
            ctx.tilemap,
            tileX,
            tileY,
            [&](int cx, int cy)
            {
                return ctx.tilemap.GetLayerTile(cx, cy, layer) >= 0 ||
                       ctx.tilemap.GetTileAnimation(cx, cy, layer) >= 0;
            },
            [&](int cx, int cy) { (ctx.tilemap.*setter)(cx, cy, layer, true); });
        std::cout << "Set " << flagName << " on " << count << " connected tiles (layer "
                  << (layer + 1) << ")" << std::endl;
    }
    else
    {
        (ctx.tilemap.*setter)(tileX, tileY, m_CurrentLayer, true);
        std::cout << "Set " << flagName << " at (" << tileX << ", " << tileY << ") layer "
                  << (m_CurrentLayer + 1) << std::endl;
    }
}

Editor::TileZoneRect Editor::CalculateParticleZoneRect(float worldX,
                                                       float worldY,
                                                       int tileWidth,
                                                       int tileHeight) const
{
    int startTileX = static_cast<int>(m_ParticleZoneStart.x / tileWidth);
    int startTileY = static_cast<int>(m_ParticleZoneStart.y / tileHeight);
    int endTileX = static_cast<int>(std::floor(worldX / tileWidth));
    int endTileY = static_cast<int>(std::floor(worldY / tileHeight));

    int minTileX = std::min(startTileX, endTileX);
    int maxTileX = std::max(startTileX, endTileX);
    int minTileY = std::min(startTileY, endTileY);
    int maxTileY = std::max(startTileY, endTileY);

    return {static_cast<float>(minTileX * tileWidth),
            static_cast<float>(minTileY * tileHeight),
            static_cast<float>((maxTileX - minTileX + 1) * tileWidth),
            static_cast<float>((maxTileY - minTileY + 1) * tileHeight)};
}
