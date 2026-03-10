#include "Editor.h"
#include "EditorDialogue.h"

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
    static bool tKeyPressed = false;
    if (glfwGetKey(ctx.window, GLFW_KEY_T) == GLFW_PRESS && !tKeyPressed && m_EditorMode)
    {
        m_ShowTilePicker = !m_ShowTilePicker;
        tKeyPressed = true;
        std::cout << "Tile picker: " << (m_ShowTilePicker ? "SHOWN" : "HIDDEN") << std::endl;

        if (m_ShowTilePicker)
        {
            // Sync smooth scrolling state to prevent jump
            m_TilePickerTargetOffsetX = m_TilePickerOffsetX;
            m_TilePickerTargetOffsetY = m_TilePickerOffsetY;
            std::vector<int> validTiles = ctx.tilemap.GetValidTileIDs();
            std::cout << "Total valid tiles available: " << validTiles.size() << std::endl;
            std::cout << "Currently selected tile ID: " << m_SelectedTileID << std::endl;
        }
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_T) == GLFW_RELEASE)
    {
        tKeyPressed = false;
    }

    // Rotates the selected tile(s) by 90 increments (0 -> 90 -> 180 -> 270).
    // Works for both single tiles and multi-tile selections when tile picker is closed.
    static bool tileRotateKeyPressed = false;
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_PRESS && !tileRotateKeyPressed && m_EditorMode &&
        !m_ShowTilePicker)
    {
        m_MultiTileRotation = (m_MultiTileRotation + 90) % 360;
        tileRotateKeyPressed = true;
        std::cout << "Tile rotation: " << m_MultiTileRotation << " degrees" << std::endl;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_RELEASE)
    {
        tileRotateKeyPressed = false;
    }

    // Pans the tile picker view using arrow keys. Shift increases speed 2.5x.
    // Uses smooth scrolling with target-based interpolation.
    if (m_EditorMode && m_ShowTilePicker)
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
            m_TilePickerTargetOffsetY += scrollSpeed;  // Scroll down (view up)
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_DOWN) == GLFW_PRESS)
        {
            m_TilePickerTargetOffsetY -= scrollSpeed;  // Scroll up (view down)
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_LEFT) == GLFW_PRESS)
        {
            m_TilePickerTargetOffsetX += scrollSpeed;  // Scroll right (view left)
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        {
            m_TilePickerTargetOffsetX -= scrollSpeed;  // Scroll left (view right)
        }

        // Calculate tile picker layout dimensions
        int dataTilesPerRow = ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();
        int dataTilesPerCol = ctx.tilemap.GetTilesetDataHeight() / ctx.tilemap.GetTileHeight();

        // Tile display size: base size * zoom factor
        // Base size is calculated to fit all tiles horizontally with 1.5x padding
        float baseTileSizePixels =
            (static_cast<float>(ctx.screenWidth) / static_cast<float>(dataTilesPerRow)) * 1.5f;
        float tileSizePixels = baseTileSizePixels * m_TilePickerZoom;

        // Total content dimensions
        float totalTilesWidth = tileSizePixels * dataTilesPerRow;
        float totalTilesHeight = tileSizePixels * dataTilesPerCol;

        // Clamp offset bounds to prevent scrolling beyond content edges
        float minOffsetX = std::min(0.0f, ctx.screenWidth - totalTilesWidth);
        float maxOffsetX = 0.0f;
        float minOffsetY = std::min(0.0f, ctx.screenHeight - totalTilesHeight);
        float maxOffsetY = 0.0f;

        m_TilePickerTargetOffsetX =
            std::max(minOffsetX, std::min(maxOffsetX, m_TilePickerTargetOffsetX));
        m_TilePickerTargetOffsetY =
            std::max(minOffsetY, std::min(maxOffsetY, m_TilePickerTargetOffsetY));
    }

    // Toggles navigation map editing. When active:
    //   - Right-click toggles navigation flags on tiles
    //   - NPC placement mode is disabled (mutually exclusive)
    //   - Cyan overlay shows navigable tiles in debug view
    //
    // Navigation tiles determine where NPCs can walk for pathfinding.
    static bool mKeyPressed = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_M) == GLFW_PRESS && !mKeyPressed)
    {
        m_EditNavigationMode = !m_EditNavigationMode;
        if (m_EditNavigationMode)
        {
            m_NPCPlacementMode = false;  // Mutually exclusive modes
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
        }
        std::cout << "Navigation edit mode: " << (m_EditNavigationMode ? "ON" : "OFF") << std::endl;
        mKeyPressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_M) == GLFW_RELEASE)
    {
        mKeyPressed = false;
    }

    // Toggles NPC placement mode. When active:
    //   - Left-click places/removes NPCs on navigation tiles
    //   - Navigation edit mode is disabled (mutually exclusive)
    //   - Use , and . keys to cycle through available NPC types
    static bool nKeyPressed = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_N) == GLFW_PRESS && !nKeyPressed)
    {
        m_NPCPlacementMode = !m_NPCPlacementMode;
        if (m_NPCPlacementMode)
        {
            m_EditNavigationMode = false;  // Mutually exclusive modes
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
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
        nKeyPressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_N) == GLFW_RELEASE)
    {
        nKeyPressed = false;
    }

    // Toggles elevation editing mode. When active:
    //   - Left-click paints elevation values (for stairs)
    //   - Right-click removes elevation (sets to 0)
    //   - Use scroll to adjust elevation value
    static bool hKeyPressed = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_H) == GLFW_PRESS && !hKeyPressed)
    {
        m_ElevationEditMode = !m_ElevationEditMode;
        if (m_ElevationEditMode)
        {
            m_EditNavigationMode = false;  // Mutually exclusive modes
            m_NPCPlacementMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
            std::cout << "Elevation edit mode: ON - Current elevation: " << m_CurrentElevation
                      << " pixels" << std::endl;
            std::cout << "Use scroll wheel to adjust elevation value" << std::endl;
        }
        else
        {
            std::cout << "Elevation edit mode: OFF" << std::endl;
        }
        hKeyPressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_H) == GLFW_RELEASE)
    {
        hKeyPressed = false;
    }

    // Toggles no-projection editing mode. When active:
    //   - Left-click sets no-projection flag (tile renders without 3D effect)
    //   - Right-click clears no-projection flag
    //   - Used for buildings that should appear to have height in 3D mode
    static bool bKeyPressedNoProj = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_B) == GLFW_PRESS && !bKeyPressedNoProj)
    {
        m_NoProjectionEditMode = !m_NoProjectionEditMode;
        if (m_NoProjectionEditMode)
        {
            m_EditNavigationMode = false;  // Mutually exclusive modes
            m_NPCPlacementMode = false;
            m_ElevationEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
            std::cout << "No-projection edit mode: ON (Layer " << m_CurrentLayer
                      << ") - Click to mark tiles that bypass 3D projection" << std::endl;
            std::cout << "Use 1-6 keys to change layer" << std::endl;
        }
        else
        {
            std::cout << "No-projection edit mode: OFF" << std::endl;
        }
        bKeyPressedNoProj = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_B) == GLFW_RELEASE)
    {
        bKeyPressedNoProj = false;
    }

    // Toggles Y-sort-plus editing mode. When active:
    //   - Left-click sets Y-sort-plus flag (tile sorts with entities by Y position)
    //   - Right-click clears Y-sort-plus flag
    //   - Used for tiles that should appear in front/behind player based on Y
    static bool yKeyPressedYSort = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_Y) == GLFW_PRESS && !yKeyPressedYSort)
    {
        m_YSortPlusEditMode = !m_YSortPlusEditMode;
        if (m_YSortPlusEditMode)
        {
            m_EditNavigationMode = false;  // Mutually exclusive modes
            m_NPCPlacementMode = false;
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
            std::cout << "Y-sort+1 edit mode: ON (Layer " << m_CurrentLayer
                      << ") - Click to mark tiles for Y-sorting with entities" << std::endl;
            std::cout << "Use 1-6 keys to change layer" << std::endl;
        }
        else
        {
            std::cout << "Y-sort-plus edit mode: OFF" << std::endl;
        }
        yKeyPressedYSort = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_Y) == GLFW_RELEASE)
    {
        yKeyPressedYSort = false;
    }

    // Toggles Y-sort-minus editing mode. When active:
    //   - Left-click sets Y-sort-minus flag (tile renders in front of player at same Y)
    //   - Right-click clears Y-sort-minus flag
    //   - Only affects tiles that are already Y-sort-plus
    static bool oKeyPressedYSortMinus = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_O) == GLFW_PRESS && !oKeyPressedYSortMinus)
    {
        m_YSortMinusEditMode = !m_YSortMinusEditMode;
        if (m_YSortMinusEditMode)
        {
            m_EditNavigationMode = false;  // Mutually exclusive modes
            m_NPCPlacementMode = false;
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
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
        oKeyPressedYSortMinus = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_O) == GLFW_RELEASE)
    {
        oKeyPressedYSortMinus = false;
    }

    // Toggles particle zone editing mode. When active:
    //   - Left-click and drag to create a particle zone
    //   - Right-click to remove zone under cursor
    //   - Use , and . keys to cycle particle type
    static bool jKeyPressedParticle = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_J) == GLFW_PRESS && !jKeyPressedParticle)
    {
        m_ParticleZoneEditMode = !m_ParticleZoneEditMode;
        if (m_ParticleZoneEditMode)
        {
            m_EditNavigationMode = false;  // Mutually exclusive modes
            m_NPCPlacementMode = false;
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_StructureEditMode = false;
            m_AnimationEditMode = false;
            const char* typeNames[] = {
                "Firefly", "Rain", "Snow", "Fog", "Sparkles", "Wisp", "Lantern", "Sunshine"};
            std::cout << "Particle zone edit mode: ON - Type: "
                      << typeNames[static_cast<int>(m_CurrentParticleType)] << std::endl;
            std::cout << "Click and drag to place zones, use , and . to change type" << std::endl;
        }
        else
        {
            std::cout << "Particle zone edit mode: OFF" << std::endl;
        }
        jKeyPressedParticle = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_J) == GLFW_RELEASE)
    {
        jKeyPressedParticle = false;
    }

    // Particle type cycling
    if (m_EditorMode && m_ParticleZoneEditMode)
    {
        static bool commaParticle = false;
        static bool periodParticle = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !commaParticle)
        {
            int type = static_cast<int>(m_CurrentParticleType);
            type = (type + 7) % 8;  // Decrement with wrap-around
            m_CurrentParticleType = static_cast<ParticleType>(type);
            const char* typeNames[] = {
                "Firefly", "Rain", "Snow", "Fog", "Sparkles", "Wisp", "Lantern", "Sunshine"};
            std::cout << "Particle type: " << typeNames[type] << std::endl;
            commaParticle = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
            commaParticle = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !periodParticle)
        {
            int type = static_cast<int>(m_CurrentParticleType);
            type = (type + 1) % 8;  // Next with wrap-around
            m_CurrentParticleType = static_cast<ParticleType>(type);
            const char* typeNames[] = {
                "Firefly", "Rain", "Snow", "Fog", "Sparkles", "Wisp", "Lantern", "Sunshine"};
            std::cout << "Particle type: " << typeNames[type] << std::endl;
            periodParticle = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
            periodParticle = false;

        // Toggles manual noProjection override for new particle zones
        // Auto-detection from tiles is always active, this is for forcing noProjection on/off
        static bool nKeyParticle = false;
        if (glfwGetKey(ctx.window, GLFW_KEY_N) == GLFW_PRESS && !nKeyParticle)
        {
            m_ParticleNoProjection = !m_ParticleNoProjection;
            std::cout << "Particle noProjection override: "
                      << (m_ParticleNoProjection ? "ON (forced)" : "OFF (auto-detect)")
                      << std::endl;
            nKeyParticle = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_N) == GLFW_RELEASE)
            nKeyParticle = false;
    }

    // Toggles structure definition mode. When active:
    //   - Click to place left anchor, click again to place right anchor
    //   - Enter to create structure from anchors
    //   - , and . to cycle through existing structures
    //   - Shift+click to assign tiles to current structure
    //   - Right-click to clear structure assignment from tiles
    //   - Delete to remove current structure
    static bool gKeyPressedStruct = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_G) == GLFW_PRESS && !gKeyPressedStruct)
    {
        m_StructureEditMode = !m_StructureEditMode;
        if (m_StructureEditMode)
        {
            m_EditNavigationMode = false;
            m_NPCPlacementMode = false;
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_AnimationEditMode = false;
            m_PlacingAnchor = 0;
            m_TempLeftAnchor = glm::vec2(-1.0f, -1.0f);
            m_TempRightAnchor = glm::vec2(-1.0f, -1.0f);
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
            m_PlacingAnchor = 0;
            std::cout << "Structure edit mode: OFF" << std::endl;
        }
        gKeyPressedStruct = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_G) == GLFW_RELEASE)
    {
        gKeyPressedStruct = false;
    }

    // Structure mode controls
    if (m_EditorMode && m_StructureEditMode)
    {
        // Cycle through structures with , and .
        static bool commaPressed = false;
        static bool periodPressed = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !commaPressed)
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
            commaPressed = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
            commaPressed = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !periodPressed)
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
            periodPressed = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
            periodPressed = false;

        // Escape to cancel anchor placement
        static bool escapePressedAnchor = false;
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapePressedAnchor &&
            m_PlacingAnchor != 0)
        {
            m_PlacingAnchor = 0;
            m_TempLeftAnchor = glm::vec2(-1.0f, -1.0f);
            m_TempRightAnchor = glm::vec2(-1.0f, -1.0f);
            std::cout << "Anchor placement cancelled" << std::endl;
            escapePressedAnchor = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
            escapePressedAnchor = false;

        // Delete to remove current structure
        static bool deletePressedStruct = false;
        if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_PRESS && !deletePressedStruct)
        {
            if (m_CurrentStructureId >= 0)
            {
                std::cout << "Removed structure " << m_CurrentStructureId << std::endl;
                ctx.tilemap.RemoveNoProjectionStructure(m_CurrentStructureId);
                m_CurrentStructureId = -1;
            }
            deletePressedStruct = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_RELEASE)
            deletePressedStruct = false;
    }

    // Toggles animated tile creation mode. When active:
    //   - Click tiles in the tile picker to add frames to animation
    //   - Press Enter to create the animation and apply to selected map tile
    //   - Press Escape to cancel/clear frames
    //   - Use , and . to adjust frame duration
    static bool kKeyPressedAnim = false;
    if (m_EditorMode && glfwGetKey(ctx.window, GLFW_KEY_K) == GLFW_PRESS && !kKeyPressedAnim)
    {
        m_AnimationEditMode = !m_AnimationEditMode;
        if (m_AnimationEditMode)
        {
            m_EditNavigationMode = false;
            m_NPCPlacementMode = false;
            m_ElevationEditMode = false;
            m_NoProjectionEditMode = false;
            m_YSortPlusEditMode = false;
            m_YSortMinusEditMode = false;
            m_ParticleZoneEditMode = false;
            m_StructureEditMode = false;
            m_AnimationFrames.clear();
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
            m_AnimationFrames.clear();
            m_SelectedAnimationId = -1;
            std::cout << "Animation edit mode: OFF" << std::endl;
        }
        kKeyPressedAnim = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_K) == GLFW_RELEASE)
    {
        kKeyPressedAnim = false;
    }

    // Animation frame duration adjustment and controls
    if (m_EditorMode && m_AnimationEditMode)
    {
        static bool commaAnim = false;
        static bool periodAnim = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !commaAnim)
        {
            m_AnimationFrameDuration = std::max(0.05f, m_AnimationFrameDuration - 0.05f);
            std::cout << "Animation frame duration: " << m_AnimationFrameDuration << "s"
                      << std::endl;
            commaAnim = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
            commaAnim = false;

        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !periodAnim)
        {
            m_AnimationFrameDuration = std::min(2.0f, m_AnimationFrameDuration + 0.05f);
            std::cout << "Animation frame duration: " << m_AnimationFrameDuration << "s"
                      << std::endl;
            periodAnim = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
            periodAnim = false;

        // Escape to clear frames and deselect animation
        static bool escAnim = false;
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escAnim)
        {
            m_AnimationFrames.clear();
            m_SelectedAnimationId = -1;
            std::cout << "Animation frames/selection cleared" << std::endl;
            escAnim = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
            escAnim = false;

        // Enter to create animation
        static bool enterAnim = false;
        if (glfwGetKey(ctx.window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterAnim)
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
            enterAnim = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_ENTER) == GLFW_RELEASE)
            enterAnim = false;
    }

    // Cycles through available NPC types when in NPC placement mode.
    // Comma (,) previous type, Period (.) next type
    // Wraps around at list boundaries.
    if (m_EditorMode && m_NPCPlacementMode && !m_AvailableNPCTypes.empty())
    {
        static bool commaPressed = false;
        static bool periodPressed = false;

        // Comma key cycles to previous NPC type
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_PRESS && !commaPressed)
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
            commaPressed = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_COMMA) == GLFW_RELEASE)
        {
            commaPressed = false;
        }

        // Period key cycles to next NPC type
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_PRESS && !periodPressed)
        {
            m_SelectedNPCTypeIndex =
                (m_SelectedNPCTypeIndex + 1) % m_AvailableNPCTypes.size();  // Wrap to start
            std::cout << "Selected NPC type: " << m_AvailableNPCTypes[m_SelectedNPCTypeIndex]
                      << " (" << (m_SelectedNPCTypeIndex + 1) << "/" << m_AvailableNPCTypes.size()
                      << ")" << std::endl;
            periodPressed = true;
        }
        if (glfwGetKey(ctx.window, GLFW_KEY_PERIOD) == GLFW_RELEASE)
        {
            periodPressed = false;
        }
    }

    // Saves the current game to save.json including:
    //   - All tile layers with rotations
    //   - Collision map
    //   - Navigation map
    //   - NPC positions, dialogues and types
    //   - Player spawn position and character type
    static bool sKeyPressed = false;
    if (glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_PRESS && !sKeyPressed && m_EditorMode)
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
        }
        sKeyPressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_RELEASE)
    {
        sKeyPressed = false;
    }

    // Reloads the game state from save.json, replacing all current state.
    // Also restores player position, character type, and recenters camera.
    static bool lKeyPressed = false;
    if (glfwGetKey(ctx.window, GLFW_KEY_L) == GLFW_PRESS && !lKeyPressed && m_EditorMode)
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
        }
        lKeyPressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_L) == GLFW_RELEASE)
    {
        lKeyPressed = false;
    }

    // Removes tiles under the mouse cursor on the currently selected layer.
    // Hold DEL and drag to delete multiple tiles continuously.
    static bool deleteKeyHeld = false;
    static int lastDeletedTileX = -1;
    static int lastDeletedTileY = -1;

    if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_PRESS && m_EditorMode && !m_ShowTilePicker)
    {
        double mouseX, mouseY;
        glfwGetCursorPos(ctx.window, &mouseX, &mouseY);
        auto st = ScreenToTileCoords(ctx, mouseX, mouseY);
        int tileX = st.tileX;
        int tileY = st.tileY;

        // Only delete if cursor moved to a new tile
        bool isNewTile = (tileX != lastDeletedTileX || tileY != lastDeletedTileY);

        // Bounds check before deletion
        if (isNewTile && tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
            tileY < ctx.tilemap.GetMapHeight())
        {
            // Delete tile on selected layer (set to -1 = empty) and clear animation
            ctx.tilemap.SetLayerTile(tileX, tileY, m_CurrentLayer, -1);
            ctx.tilemap.SetTileAnimation(tileX, tileY, static_cast<int>(m_CurrentLayer), -1);
            lastDeletedTileX = tileX;
            lastDeletedTileY = tileY;
        }
        deleteKeyHeld = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_DELETE) == GLFW_RELEASE)
    {
        deleteKeyHeld = false;
        lastDeletedTileX = -1;
        lastDeletedTileY = -1;
    }

    // Rotates the tile under the mouse cursor by 90 on the current layer.
    // Note: This is different from multi-tile rotation which uses R when
    //       m_MultiTileSelectionMode is true.
    static bool rKeyPressed = false;
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_PRESS && !rKeyPressed && m_EditorMode &&
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
        rKeyPressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_RELEASE)
    {
        rKeyPressed = false;
    }

    // Selects which tile layer to edit.
    static bool key1Pressed = false;
    static bool key2Pressed = false;
    static bool key3Pressed = false;
    static bool key4Pressed = false;
    static bool key5Pressed = false;
    static bool key6Pressed = false;
    static bool key7Pressed = false;
    static bool key8Pressed = false;
    static bool key9Pressed = false;
    static bool key0Pressed = false;

    // Layer switching: Keys 1-9,0 map to dynamic layers 0-9
    if (glfwGetKey(ctx.window, GLFW_KEY_1) == GLFW_PRESS && !key1Pressed && m_EditorMode)
    {
        m_CurrentLayer = 0;
        std::cout << "Switched to Layer 1: Ground (background)" << std::endl;
        key1Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_1) == GLFW_RELEASE)
        key1Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_2) == GLFW_PRESS && !key2Pressed && m_EditorMode)
    {
        m_CurrentLayer = 1;
        std::cout << "Switched to Layer 2: Ground Detail (background)" << std::endl;
        key2Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_2) == GLFW_RELEASE)
        key2Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_3) == GLFW_PRESS && !key3Pressed && m_EditorMode)
    {
        m_CurrentLayer = 2;
        std::cout << "Switched to Layer 3: Objects (background)" << std::endl;
        key3Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_3) == GLFW_RELEASE)
        key3Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_4) == GLFW_PRESS && !key4Pressed && m_EditorMode)
    {
        m_CurrentLayer = 3;
        std::cout << "Switched to Layer 4: Objects2 (background)" << std::endl;
        key4Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_4) == GLFW_RELEASE)
        key4Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_5) == GLFW_PRESS && !key5Pressed && m_EditorMode)
    {
        m_CurrentLayer = 4;
        std::cout << "Switched to Layer 5: Objects3 (background)" << std::endl;
        key5Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_5) == GLFW_RELEASE)
        key5Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_6) == GLFW_PRESS && !key6Pressed && m_EditorMode)
    {
        m_CurrentLayer = 5;
        std::cout << "Switched to Layer 6: Foreground (foreground)" << std::endl;
        key6Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_6) == GLFW_RELEASE)
        key6Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_7) == GLFW_PRESS && !key7Pressed && m_EditorMode)
    {
        m_CurrentLayer = 6;
        std::cout << "Switched to Layer 7: Foreground2 (foreground)" << std::endl;
        key7Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_7) == GLFW_RELEASE)
        key7Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_8) == GLFW_PRESS && !key8Pressed && m_EditorMode)
    {
        m_CurrentLayer = 7;
        std::cout << "Switched to Layer 8: Overlay (foreground)" << std::endl;
        key8Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_8) == GLFW_RELEASE)
        key8Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_9) == GLFW_PRESS && !key9Pressed && m_EditorMode)
    {
        m_CurrentLayer = 8;
        std::cout << "Switched to Layer 9: Overlay2 (foreground)" << std::endl;
        key9Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_9) == GLFW_RELEASE)
        key9Pressed = false;

    if (glfwGetKey(ctx.window, GLFW_KEY_0) == GLFW_PRESS && !key0Pressed && m_EditorMode)
    {
        m_CurrentLayer = 9;
        std::cout << "Switched to Layer 10: Overlay3 (foreground)" << std::endl;
        key0Pressed = true;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_0) == GLFW_RELEASE)
        key0Pressed = false;
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
            (tileX != m_LastNavigationTileX || tileY != m_LastNavigationTileY);
        bool isNewCollisionTilePosition =
            (tileX != m_LastCollisionTileX || tileY != m_LastCollisionTileY);

        if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
            tileY < ctx.tilemap.GetMapHeight())
        {
            // Animation edit mode, right-click removes animation from tile
            if (m_AnimationEditMode)
            {
                int currentAnim = ctx.tilemap.GetTileAnimation(tileX, tileY, m_CurrentLayer);
                if (currentAnim >= 0)
                {
                    ctx.tilemap.SetTileAnimation(tileX, tileY, m_CurrentLayer, -1);
                    std::cout << "Removed animation from tile (" << tileX << ", " << tileY
                              << ") on layer " << m_CurrentLayer << std::endl;
                }
                m_RightMousePressed = true;
                return;
            }
            // Elevation edit mode, right-click clears elevation at tile
            else if (m_ElevationEditMode)
            {
                ctx.tilemap.SetElevation(tileX, tileY, 0);
                std::cout << "Cleared elevation at (" << tileX << ", " << tileY << ")" << std::endl;
                m_RightMousePressed = true;
            }
            // Structure edit mode, right-click clears structure assignment from tiles
            // Shift+right-click, flood-fill to clear all connected tiles
            else if (m_StructureEditMode)
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
                m_RightMousePressed = true;
            }
            // No-projection edit mode, right-click clears no-projection flag for current layer
            // Shift+right-click, flood-fill to clear all connected tiles
            else if (m_NoProjectionEditMode)
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
                m_RightMousePressed = true;
            }
            // Y-sort-plus edit mode, right-click clears Y-sort-plus flag for current layer
            // Shift+right-click, flood-fill to clear all connected tiles
            else if (m_YSortPlusEditMode)
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
                        { return ctx.tilemap.GetLayerYSortPlus(cx, cy, layer); },
                        [&](int cx, int cy)
                        { ctx.tilemap.SetLayerYSortPlus(cx, cy, layer, false); });
                    std::cout << "Cleared Y-sort-plus on " << count << " connected tiles (layer "
                              << (layer + 1) << ")" << std::endl;
                }
                else
                {
                    ctx.tilemap.SetLayerYSortPlus(tileX, tileY, m_CurrentLayer, false);
                    std::cout << "Cleared Y-sort-plus at (" << tileX << ", " << tileY << ") layer "
                              << (m_CurrentLayer + 1) << std::endl;
                }
                m_RightMousePressed = true;
            }
            // Y-sort-minus edit mode, right-click clears Y-sort-minus flag for current layer
            // Shift+right-click, flood-fill to clear all connected tiles
            else if (m_YSortMinusEditMode)
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
                        { return ctx.tilemap.GetLayerYSortMinus(cx, cy, layer); },
                        [&](int cx, int cy)
                        { ctx.tilemap.SetLayerYSortMinus(cx, cy, layer, false); });
                    std::cout << "Cleared Y-sort-minus on " << count << " connected tiles (layer "
                              << (layer + 1) << ")" << std::endl;
                }
                else
                {
                    ctx.tilemap.SetLayerYSortMinus(tileX, tileY, m_CurrentLayer, false);
                    std::cout << "Cleared Y-sort-minus at (" << tileX << ", " << tileY << ") layer "
                              << (m_CurrentLayer + 1) << std::endl;
                }
                m_RightMousePressed = true;
            }
            // Particle zone edit mode, right-click removes zone under cursor
            else if (m_ParticleZoneEditMode)
            {
                // Find zone under cursor and remove it
                auto* zones = ctx.tilemap.GetParticleZonesMutable();
                for (size_t i = 0; i < zones->size(); ++i)
                {
                    const ParticleZone& zone = (*zones)[i];
                    if (worldX >= zone.position.x && worldX < zone.position.x + zone.size.x &&
                        worldY >= zone.position.y && worldY < zone.position.y + zone.size.y)
                    {
                        const char* typeNames[] = {"Firefly",
                                                   "Rain",
                                                   "Snow",
                                                   "Fog",
                                                   "Sparkles",
                                                   "Wisp",
                                                   "Lantern",
                                                   "Sunshine"};
                        std::cout << "Removed " << typeNames[static_cast<int>(zone.type)]
                                  << " zone at (" << zone.position.x << ", " << zone.position.y
                                  << ")" << std::endl;
                        ctx.particles.OnZoneRemoved(static_cast<int>(i));
                        ctx.tilemap.RemoveParticleZone(i);
                        break;
                    }
                }
                m_RightMousePressed = true;
            }
            else if (m_EditNavigationMode)
            {
                // Navigation editing mode, support drag-to-draw
                bool navigationChanged = false;
                if (!m_RightMousePressed)
                {
                    // Initial click determines target state
                    bool walkable = ctx.tilemap.GetNavigation(tileX, tileY);
                    m_NavigationDragState = !walkable;  // Set to opposite of current state
                    ctx.tilemap.SetNavigation(tileX, tileY, m_NavigationDragState);
                    navigationChanged = true;
                    std::cout << "=== NAVIGATION DRAG START ===" << std::endl;
                    std::cout << "Tile (" << tileX << ", " << tileY
                              << "): " << (walkable ? "ON" : "OFF") << " -> "
                              << (m_NavigationDragState ? "ON" : "OFF") << std::endl;
                    m_LastNavigationTileX = tileX;
                    m_LastNavigationTileY = tileY;
                    m_RightMousePressed = true;
                }
                else if (isNewNavigationTilePosition)
                {
                    // Dragging sets navigation to the same state as initial click
                    bool currentWalkable = ctx.tilemap.GetNavigation(tileX, tileY);
                    if (currentWalkable != m_NavigationDragState)
                    {
                        ctx.tilemap.SetNavigation(tileX, tileY, m_NavigationDragState);
                        navigationChanged = true;
                        std::cout << "Navigation drag: Tile (" << tileX << ", " << tileY << ") -> "
                                  << (m_NavigationDragState ? "ON" : "OFF") << std::endl;
                    }
                    m_LastNavigationTileX = tileX;
                    m_LastNavigationTileY = tileY;
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
                if (!m_RightMousePressed)
                {
                    // Initial click determines target state
                    bool currentCollision = ctx.tilemap.GetTileCollision(tileX, tileY);
                    m_CollisionDragState = !currentCollision;  // Set to opposite of current state
                    ctx.tilemap.SetTileCollision(tileX, tileY, m_CollisionDragState);
                    std::cout << "=== COLLISION DRAG START ===" << std::endl;
                    std::cout << "Tile (" << tileX << ", " << tileY
                              << "): " << (currentCollision ? "ON" : "OFF") << " -> "
                              << (m_CollisionDragState ? "ON" : "OFF") << std::endl;
                    m_LastCollisionTileX = tileX;
                    m_LastCollisionTileY = tileY;
                    m_RightMousePressed = true;
                }
                else if (isNewCollisionTilePosition)
                {
                    // Dragging sets collision to the same state as initial click
                    bool currentCollision = ctx.tilemap.GetTileCollision(tileX, tileY);
                    if (currentCollision != m_CollisionDragState)
                    {
                        ctx.tilemap.SetTileCollision(tileX, tileY, m_CollisionDragState);
                        std::cout << "Collision drag: Tile (" << tileX << ", " << tileY << ") -> "
                                  << (m_CollisionDragState ? "ON" : "OFF") << std::endl;
                    }
                    m_LastCollisionTileX = tileX;
                    m_LastCollisionTileY = tileY;
                }
            }
        }
        else
        {
            if (!m_RightMousePressed)
            {
                std::cout << "Right-click outside map bounds (tileX=" << tileX << " tileY=" << tileY
                          << " map size=" << ctx.tilemap.GetMapWidth() << "x"
                          << ctx.tilemap.GetMapHeight() << ")" << std::endl;
            }
        }
    }
    else if (!rightMouseDown)
    {
        m_RightMousePressed = false;
        // Reset navigation and collision drag tracking when mouse is released
        m_LastNavigationTileX = -1;
        m_LastNavigationTileY = -1;
        m_LastCollisionTileX = -1;
        m_LastCollisionTileY = -1;
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
        float tileSize = baseTileSize * m_TilePickerZoom;

        // Start selection on mouse down
        if (leftMouseDown && !m_MousePressed && !m_IsSelectingTiles)
        {
            if (mouseX >= 0 && mouseX < ctx.screenWidth && mouseY >= 0 && mouseY < ctx.screenHeight)
            {
                // Account for offset when calculating tile position
                double adjustedMouseX = mouseX - m_TilePickerOffsetX;
                double adjustedMouseY = mouseY - m_TilePickerOffsetY;
                int pickerTileX = static_cast<int>(adjustedMouseX / tileSize);
                int pickerTileY = static_cast<int>(adjustedMouseY / tileSize);
                int localIndex = pickerTileY * tilesPerRow + pickerTileX;
                int clickedTileID = localIndex;

                if (clickedTileID >= 0 && clickedTileID < totalTiles)
                {
                    // Animation edit mode, collect frames instead of normal selection
                    if (m_AnimationEditMode)
                    {
                        // Add frame to animation
                        m_AnimationFrames.push_back(clickedTileID);
                        m_MousePressed = true;
                        std::cout << "Added animation frame: " << clickedTileID
                                  << " (total frames: " << m_AnimationFrames.size() << ")"
                                  << std::endl;
                    }
                    else
                    {
                        m_IsSelectingTiles = true;
                        m_SelectionStartTileID = clickedTileID;
                        m_SelectedTileID = clickedTileID;
                        m_MousePressed = true;  // Prevent other click handlers from firing
                        std::cout << "Started selection at tile ID: " << clickedTileID
                                  << " (mouse: " << mouseX << ", " << mouseY
                                  << ", adjusted: " << adjustedMouseX << ", " << adjustedMouseY
                                  << ", offset: " << m_TilePickerOffsetX << ", "
                                  << m_TilePickerOffsetY << ")" << std::endl;
                    }
                }
            }
        }

        // Update selection while dragging
        if (leftMouseDown && m_IsSelectingTiles)
        {
            if (mouseX >= 0 && mouseX < ctx.screenWidth && mouseY >= 0 && mouseY < ctx.screenHeight)
            {
                // Account for offset when calculating tile position
                double adjustedMouseX = mouseX - m_TilePickerOffsetX;
                double adjustedMouseY = mouseY - m_TilePickerOffsetY;
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
        if (!leftMouseDown && m_AnimationEditMode && m_MousePressed)
        {
            m_MousePressed = false;
        }

        // Finish selection on mouse up
        if (!leftMouseDown && m_IsSelectingTiles)
        {
            if (m_SelectionStartTileID >= 0)
            {
                int startTileID = m_SelectionStartTileID;
                int endTileID = m_SelectedTileID;

                int startX = startTileID % dataTilesPerRow;
                int startY = startTileID / dataTilesPerRow;
                int endX = endTileID % dataTilesPerRow;
                int endY = endTileID / dataTilesPerRow;

                int minX = std::min(startX, endX);
                int maxX = std::max(startX, endX);
                int minY = std::min(startY, endY);
                int maxY = std::max(startY, endY);

                m_SelectedTileStartID = minY * dataTilesPerRow + minX;
                m_SelectedTileWidth = maxX - minX + 1;
                m_SelectedTileHeight = maxY - minY + 1;

                if (m_SelectedTileWidth > 1 || m_SelectedTileHeight > 1)
                {
                    // Multi-tile selection, enable placement mode,
                    // but do not change the world camera or zoom.
                    m_MultiTileSelectionMode = true;
                    m_IsPlacingMultiTile = true;
                    m_MultiTileRotation = 0;  // Reset rotation for new selection
                    std::cout << "=== MULTI-TILE SELECTION ===" << std::endl;
                    std::cout << "Start tile ID: " << m_SelectedTileStartID << std::endl;
                    std::cout << "Size: " << m_SelectedTileWidth << "x" << m_SelectedTileHeight
                              << std::endl;
                }
                else
                {
                    m_MultiTileSelectionMode = false;
                    m_IsPlacingMultiTile = false;
                    m_MultiTileRotation = 0;  // Reset rotation
                    std::cout << "=== SINGLE TILE SELECTION ===" << std::endl;
                    std::cout << "Tile ID: " << m_SelectedTileStartID << std::endl;
                }

                m_ShowTilePicker = false;
            }
            m_IsSelectingTiles = false;
            m_SelectionStartTileID = -1;
            m_MousePressed = false;  // Reset mouse pressed state
        }

        // Early return to prevent tile placement when tile picker is shown
        if (m_ShowTilePicker)
        {
            // Update mouse position for preview
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;
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
        if (m_EditorMode && m_NPCPlacementMode)
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                // Only process if this is a new tile
                if (tileX == m_LastNPCPlacementTileX && tileY == m_LastNPCPlacementTileY)
                {
                    return;  // Already processed this tile during this click
                }
                m_LastNPCPlacementTileX = tileX;
                m_LastNPCPlacementTileY = tileY;

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
                            std::uniform_int_distribution<int> dist(0, 6);
                            int dialogueType = dist(rng);
                            if (dialogueType < 5)
                            {
                                BuildMysteryDialogueTree(
                                    tree, npcName, kMysteryDialogues[dialogueType]);
                            }
                            else if (dialogueType == 5)
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
        if (m_EditorMode && m_ParticleZoneEditMode)
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
        if (m_EditorMode && m_AnimationEditMode && m_SelectedAnimationId >= 0)
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
        if (m_EditorMode && m_ElevationEditMode)
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
        if (m_EditorMode && m_StructureEditMode)
        {
            if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                tileY < ctx.tilemap.GetMapHeight())
            {
                bool shiftHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
                bool ctrlHeld = (glfwGetKey(ctx.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                 glfwGetKey(ctx.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

                if (ctrlHeld && !m_MousePressed)
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
                        m_MousePressed = true;
                        std::cout << "Left anchor: " << cornerNames[cornerIdx] << " of tile ("
                                  << tileX << ", " << tileY << ")" << std::endl;
                    }
                    else if (m_PlacingAnchor == 2)
                    {
                        // Place right anchor and create structure
                        m_TempRightAnchor = glm::vec2(cornerX, cornerY);
                        m_PlacingAnchor = 0;
                        m_MousePressed = true;

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
                else if (shiftHeld && !m_MousePressed)
                {
                    // Shift+click: flood-fill set no-projection and assign to structure
                    m_MousePressed = true;
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
                else if (!ctrlHeld && !shiftHeld && !m_MousePressed)
                {
                    // Normal click: toggle no-projection on single tile
                    m_MousePressed = true;
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
        if (m_EditorMode && m_NoProjectionEditMode)
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
        if (m_EditorMode && m_YSortPlusEditMode)
        {
            SetLayerFlagAtTile(ctx, tileX, tileY, &Tilemap::SetLayerYSortPlus, "Y-sort-plus");
            return;
        }

        // Y-sort-minus editing mode, set Y-sort-minus flag for current layer
        // Shift+click, flood-fill to mark all connected tiles in the shape
        if (m_EditorMode && m_YSortMinusEditMode)
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
        bool isNewTilePosition = (tileX != m_LastPlacedTileX || tileY != m_LastPlacedTileY);

        if (m_MultiTileSelectionMode)
        {
            // Multi-tile placement, only place on initial click, not on drag
            if (!m_MousePressed)
            {
                int dataTilesPerRow =
                    ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();

                int rotatedWidth = (m_MultiTileRotation == 90 || m_MultiTileRotation == 270)
                                       ? m_SelectedTileHeight
                                       : m_SelectedTileWidth;
                int rotatedHeight = (m_MultiTileRotation == 90 || m_MultiTileRotation == 270)
                                        ? m_SelectedTileWidth
                                        : m_SelectedTileHeight;
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
                            m_SelectedTileStartID + sourceDy * dataTilesPerRow + sourceDx;

                        if (placeX >= 0 && placeX < ctx.tilemap.GetMapWidth() && placeY >= 0 &&
                            placeY < ctx.tilemap.GetMapHeight())
                        {
                            ctx.tilemap.SetLayerTile(placeX, placeY, m_CurrentLayer, sourceTileID);
                            ctx.tilemap.SetLayerRotation(
                                placeX, placeY, m_CurrentLayer, tileRotation);
                        }
                    }
                }
                std::cout << "Placed " << m_SelectedTileWidth << "x" << m_SelectedTileHeight
                          << " tiles starting at (" << tileX << ", " << tileY << ") on layer "
                          << (m_CurrentLayer + 1) << std::endl;

                // Keep multi-tile selection active for multiple placements
                m_LastPlacedTileX = tileX;
                m_LastPlacedTileY = tileY;
                m_MousePressed = true;
            }
        }
        else
        {
            // Single tile placement, support drag-to-place with rotation
            if (isNewTilePosition || !m_MousePressed)
            {
                if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
                    tileY < ctx.tilemap.GetMapHeight())
                {
                    float tileRotation = GetCompensatedTileRotation();
                    ctx.tilemap.SetLayerTile(tileX, tileY, m_CurrentLayer, m_SelectedTileStartID);
                    ctx.tilemap.SetLayerRotation(tileX, tileY, m_CurrentLayer, tileRotation);

                    m_LastPlacedTileX = tileX;
                    m_LastPlacedTileY = tileY;
                    m_MousePressed = true;
                }
            }
        }
    }

    // Reset mouse pressed state and last placed tile position when mouse button is released
    if (!leftMouseDown)
    {
        // Finalize particle zone placement on mouse release
        if (m_PlacingParticleZone && m_ParticleZoneEditMode)
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

            const char* typeNames[] = {
                "Firefly", "Rain", "Snow", "Fog", "Sparkles", "Wisp", "Lantern", "Sunshine"};
            std::cout << "Created " << typeNames[static_cast<int>(m_CurrentParticleType)]
                      << " zone at (" << zr.x << ", " << zr.y << ") size " << zr.w << "x" << zr.h;
            if (hasNoProjection)
                std::cout << " [noProjection]";
            std::cout << std::endl;

            m_PlacingParticleZone = false;
        }

        m_MousePressed = false;
        m_LastPlacedTileX = -1;
        m_LastPlacedTileY = -1;
        m_LastNPCPlacementTileX = -1;
        m_LastNPCPlacementTileY = -1;
    }

    // Update mouse position for preview
    m_LastMouseX = mouseX;
    m_LastMouseY = mouseY;
}

void Editor::HandleScroll(double yoffset, const EditorContext& ctx)
{
    // Check for Ctrl modifier
    int ctrlState = glfwGetKey(ctx.window, GLFW_KEY_LEFT_CONTROL) |
                    glfwGetKey(ctx.window, GLFW_KEY_RIGHT_CONTROL);

    // Elevation adjustment with scroll wheel when in elevation edit mode
    if (m_ElevationEditMode && ctrlState != GLFW_PRESS)
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

            float oldTileSize = baseTileSizePixels * m_TilePickerZoom;

            float adjustedMouseX = static_cast<float>(mouseX) - m_TilePickerOffsetX;
            float adjustedMouseY = static_cast<float>(mouseY) - m_TilePickerOffsetY;
            int pickerTileX = static_cast<int>(adjustedMouseX / oldTileSize);
            int pickerTileY = static_cast<int>(adjustedMouseY / oldTileSize);

            float zoomDelta = yoffset > 0 ? 1.1f : 0.9f;
            m_TilePickerZoom *= zoomDelta;
            m_TilePickerZoom = std::max(0.25f, std::min(8.0f, m_TilePickerZoom));

            float newTileSize = baseTileSizePixels * m_TilePickerZoom;

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
            m_TilePickerOffsetX = newOffsetX;
            m_TilePickerOffsetY = newOffsetY;
            m_TilePickerTargetOffsetX = newOffsetX;
            m_TilePickerTargetOffsetY = newOffsetY;

            std::cout << "Tile picker zoom: " << m_TilePickerZoom
                      << "x (offset: " << m_TilePickerOffsetX << ", " << m_TilePickerOffsetY << ")"
                      << std::endl;
        }
        else
        {
            // Vertical pan with scroll wheel
            float panAmount = static_cast<float>(yoffset) * 200.0f;
            m_TilePickerTargetOffsetY += panAmount;

            float tileSizePixels = baseTileSizePixels * m_TilePickerZoom;
            float totalTilesWidth = tileSizePixels * dataTilesPerRow;
            float totalTilesHeight = tileSizePixels * dataTilesPerCol;
            float minOffsetX = ctx.screenWidth - totalTilesWidth;
            float maxOffsetX = 0.0f;
            float minOffsetY = ctx.screenHeight - totalTilesHeight;
            float maxOffsetY = 0.0f;

            m_TilePickerTargetOffsetX =
                std::max(minOffsetX, std::min(maxOffsetX, m_TilePickerTargetOffsetX));
            m_TilePickerTargetOffsetY =
                std::max(minOffsetY, std::min(maxOffsetY, m_TilePickerTargetOffsetY));
        }
    }
}

void Editor::CalculateRotatedSourceTile(int dx, int dy, int& sourceDx, int& sourceDy) const
{
    if (m_MultiTileRotation == 0)
    {
        sourceDx = dx;
        sourceDy = dy;
    }
    else if (m_MultiTileRotation == 90)
    {
        sourceDx = m_SelectedTileWidth - 1 - dy;
        sourceDy = dx;
    }
    else if (m_MultiTileRotation == 180)
    {
        sourceDx = m_SelectedTileWidth - 1 - dx;
        sourceDy = m_SelectedTileHeight - 1 - dy;
    }
    else  // 270 degrees
    {
        sourceDx = dy;
        sourceDy = m_SelectedTileHeight - 1 - dx;
    }
}

float Editor::GetCompensatedTileRotation() const
{
    float tileRotation = static_cast<float>(m_MultiTileRotation);
    if (m_MultiTileRotation == 90 || m_MultiTileRotation == 270)
        tileRotation = static_cast<float>((m_MultiTileRotation + 180) % 360);
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
