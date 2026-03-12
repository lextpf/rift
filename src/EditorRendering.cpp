#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace
{

struct VisibleTileRange
{
    int tileWidth, tileHeight;
    glm::vec2 screenSize;
    int startX, endX, startY, endY;
};

VisibleTileRange CalcVisibleTileRange(const EditorContext& ctx)
{
    VisibleTileRange r{};
    r.tileWidth = ctx.tilemap.GetTileWidth();
    r.tileHeight = ctx.tilemap.GetTileHeight();
    if (r.tileWidth <= 0 || r.tileHeight <= 0)
        return r;
    float zoom = std::max(ctx.cameraZoom, 0.01f);
    float worldWidth = static_cast<float>(ctx.tilesVisibleWidth * r.tileWidth) / zoom;
    float worldHeight = static_cast<float>(ctx.tilesVisibleHeight * r.tileHeight) / zoom;
    r.screenSize = glm::vec2(worldWidth, worldHeight);
    r.startX = std::max(0, static_cast<int>(ctx.cameraPosition.x / r.tileWidth) - 1);
    r.endX = std::min(ctx.tilemap.GetMapWidth(),
                      static_cast<int>((ctx.cameraPosition.x + worldWidth) / r.tileWidth) + 1);
    r.startY = std::max(0, static_cast<int>(ctx.cameraPosition.y / r.tileHeight) - 1);
    r.endY = std::min(ctx.tilemap.GetMapHeight(),
                      static_cast<int>((ctx.cameraPosition.y + worldHeight) / r.tileHeight) + 1);
    return r;
}

struct StructureBounds
{
    int minX, maxX, minY, maxY;
};

StructureBounds FloodFillNoProjBounds(const Tilemap& tilemap,
                                      int startX,
                                      int startY,
                                      int mapWidth,
                                      int mapHeight,
                                      size_t layerCount,
                                      std::vector<char>& processed)
{
    StructureBounds bounds{startX, startX, startY, startY};
    std::vector<std::pair<int, int>> stack;
    stack.push_back({startX, startY});

    while (!stack.empty())
    {
        auto [cx, cy] = stack.back();
        stack.pop_back();

        if (cx < 0 || cx >= mapWidth || cy < 0 || cy >= mapHeight)
            continue;

        int cIdx = cy * mapWidth + cx;
        if (processed[cIdx])
            continue;

        bool isNoProj = false;
        for (size_t li = 0; li < layerCount; ++li)
        {
            if (tilemap.GetLayerNoProjection(cx, cy, li))
            {
                isNoProj = true;
                break;
            }
        }
        if (!isNoProj)
            continue;

        processed[cIdx] = 1;
        bounds.minX = std::min(bounds.minX, cx);
        bounds.maxX = std::max(bounds.maxX, cx);
        bounds.minY = std::min(bounds.minY, cy);
        bounds.maxY = std::max(bounds.maxY, cy);

        stack.push_back({cx - 1, cy});
        stack.push_back({cx + 1, cy});
        stack.push_back({cx, cy - 1});
        stack.push_back({cx, cy + 1});
    }

    return bounds;
}

void DrawCrossMarker(IRenderer& renderer, const glm::vec2& pos, float size, const glm::vec4& color)
{
    renderer.DrawColoredRect(
        glm::vec2(pos.x - size, pos.y - 1.0f), glm::vec2(size * 2.0f, 2.0f), color);
    renderer.DrawColoredRect(
        glm::vec2(pos.x - 1.0f, pos.y - size), glm::vec2(2.0f, size * 2.0f), color);
}

glm::vec4 GetParticleTypeColor(ParticleType type, float alpha)
{
    switch (type)
    {
        case ParticleType::Firefly:
            return glm::vec4(1.0f, 0.9f, 0.2f, alpha);
        case ParticleType::Rain:
            return glm::vec4(0.3f, 0.5f, 1.0f, alpha);
        case ParticleType::Snow:
            return glm::vec4(0.9f, 0.9f, 1.0f, alpha);
        case ParticleType::Fog:
            return glm::vec4(0.7f, 0.7f, 0.8f, alpha);
        case ParticleType::Sparkles:
            return glm::vec4(1.0f, 1.0f, 0.5f, alpha);
        case ParticleType::Wisp:
            return glm::vec4(0.5f, 0.8f, 1.0f, alpha);
        case ParticleType::Lantern:
            return glm::vec4(1.0f, 0.6f, 0.3f, alpha);
        case ParticleType::Sunshine:
            return glm::vec4(1.0f, 0.95f, 0.6f, alpha);
    }
    return glm::vec4(1.0f, 1.0f, 1.0f, alpha);
}

}  // anonymous namespace

void Editor::EnsureNoProjBoundsCache(const EditorContext& ctx)
{
    if (m_NoProjBoundsCached)
        return;
    m_NoProjBoundsCached = true;
    m_CachedNoProjBounds.clear();

    int mapWidth = ctx.tilemap.GetMapWidth();
    int mapHeight = ctx.tilemap.GetMapHeight();
    size_t layerCount = ctx.tilemap.GetLayerCount();

    std::vector<char> processed(static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight), 0);

    for (int y = 0; y < mapHeight; ++y)
    {
        for (int x = 0; x < mapWidth; ++x)
        {
            int idx = y * mapWidth + x;
            if (processed[idx])
                continue;

            bool isNoProj = false;
            for (size_t li = 0; li < layerCount; ++li)
            {
                if (ctx.tilemap.GetLayerNoProjection(x, y, li))
                {
                    isNoProj = true;
                    break;
                }
            }
            if (!isNoProj)
                continue;

            auto [minX, maxX, minY, maxY] = FloodFillNoProjBounds(
                ctx.tilemap, x, y, mapWidth, mapHeight, layerCount, processed);

            m_CachedNoProjBounds.push_back({minX, maxX, minY, maxY});
        }
    }
}

void Editor::RenderCollisionOverlays(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);

    // Render red overlay for each collision tile
    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            if (ctx.tilemap.GetTileCollision(x, y))
            {
                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);

                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
            }
        }
    }

    // Render player hitbox
    glm::vec2 playerPos = ctx.player.GetPosition();

    glm::vec2 playerHitboxPos(
        playerPos.x - PlayerCharacter::HITBOX_WIDTH * 0.5f - ctx.cameraPosition.x,
        playerPos.y - PlayerCharacter::HITBOX_HEIGHT - ctx.cameraPosition.y);
    glm::vec2 playerHitboxSize(PlayerCharacter::HITBOX_WIDTH, PlayerCharacter::HITBOX_HEIGHT);

    if (playerHitboxPos.x + playerHitboxSize.x >= 0 && playerHitboxPos.x <= vr.screenSize.x &&
        playerHitboxPos.y + playerHitboxSize.y >= 0 && playerHitboxPos.y <= vr.screenSize.y)
    {
        ctx.renderer.DrawColoredRect(
            playerHitboxPos, playerHitboxSize, glm::vec4(1.0f, 1.0f, 0.0f, 0.6f));
    }

    // Render NPC hitboxes in editor mode
    const float NPC_HITBOX_SIZE = PlayerCharacter::HITBOX_HEIGHT;
    for (const auto& npc : ctx.npcs)
    {
        glm::vec2 npcFeet = npc.GetPosition();
        glm::vec2 npcHitboxPos(npcFeet.x - NPC_HITBOX_SIZE * 0.5f - ctx.cameraPosition.x,
                               npcFeet.y - NPC_HITBOX_SIZE - ctx.cameraPosition.y);
        glm::vec2 npcHitboxSize(NPC_HITBOX_SIZE, NPC_HITBOX_SIZE);

        if (npcHitboxPos.x + npcHitboxSize.x >= 0 && npcHitboxPos.x <= vr.screenSize.x &&
            npcHitboxPos.y + npcHitboxSize.y >= 0 && npcHitboxPos.y <= vr.screenSize.y)
        {
            ctx.renderer.DrawColoredRect(
                npcHitboxPos, npcHitboxSize, glm::vec4(1.0f, 0.0f, 1.0f, 0.6f));
        }
    }
}

void Editor::RenderNavigationOverlays(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);

    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            if (!ctx.tilemap.GetNavigation(x, y))
                continue;

            glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                              y * vr.tileHeight - ctx.cameraPosition.y);

            ctx.renderer.DrawColoredRect(
                tilePos,
                glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                glm::vec4(0.0f, 1.0f, 1.0f, 0.3f));
        }
    }
}

void Editor::RenderElevationOverlays(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);

    bool perspectiveEnabled = ctx.renderer.GetPerspectiveState().enabled;

    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            int elevation = ctx.tilemap.GetElevation(x, y);
            if (elevation <= 0)
                continue;

            glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                              y * vr.tileHeight - ctx.cameraPosition.y);

            float alpha = std::min(0.5f, static_cast<float>(elevation) / 32.0f * 0.5f + 0.15f);

            ctx.renderer.DrawColoredRect(
                tilePos,
                glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                glm::vec4(0.8f, 0.2f, 0.8f, alpha));

            if (!perspectiveEnabled)
            {
                std::string elevText = std::to_string(elevation);
                float textScale = 0.2f;
                float textWidth = elevText.length() * 8.0f * textScale;
                float textX = tilePos.x + (vr.tileWidth - textWidth) * 0.5f;
                float textY = tilePos.y + vr.tileHeight * 0.6f;
                ctx.renderer.DrawText(elevText,
                                      glm::vec2(textX, textY),
                                      textScale,
                                      glm::vec3(1.0f, 1.0f, 0.2f),
                                      0.0f,
                                      0.15f);
            }
        }
    }
}

void Editor::RenderNoProjectionOverlays(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);

    size_t layerCount = ctx.tilemap.GetLayerCount();

    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            // In no-projection edit mode, only show flags for current layer
            if (m_NoProjectionEditMode)
            {
                if (!ctx.tilemap.GetLayerNoProjection(x, y, m_CurrentLayer))
                    continue;

                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);

                // Orange overlay for no-projection tiles
                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    glm::vec4(1.0f, 0.6f, 0.0f, 0.5f));
            }
            else
            {
                // Check tile for all layers
                int count = 0;
                for (size_t layer = 0; layer < layerCount; ++layer)
                {
                    if (ctx.tilemap.GetLayerNoProjection(x, y, layer))
                        count++;
                }

                if (count == 0)
                    continue;

                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);

                // Alpha based on number of layers with flag
                float alpha =
                    0.15f + (static_cast<float>(count) / static_cast<float>(layerCount)) * 0.35f;

                // Orange overlay for no-projection tiles
                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    glm::vec4(1.0f, 0.6f, 0.0f, alpha));
            }
        }
    }

    // Draw anchor markers for detected no-projection structures (2D mode only)
    auto perspState = ctx.renderer.GetPerspectiveState();
    if (!perspState.enabled)
    {
        EnsureNoProjBoundsCache(ctx);

        float markerSize = 6.0f;
        glm::vec4 anchorColor(0.0f, 1.0f, 0.0f, 1.0f);

        for (const auto& bounds : m_CachedNoProjBounds)
        {
            int leftPixelX = bounds.minX * vr.tileWidth;
            int rightPixelX = (bounds.maxX + 1) * vr.tileWidth;
            int bottomPixelY = (bounds.maxY + 1) * vr.tileHeight;

            glm::vec2 anchorLeft(static_cast<float>(leftPixelX) - ctx.cameraPosition.x,
                                 static_cast<float>(bottomPixelY) - ctx.cameraPosition.y);
            glm::vec2 anchorRight(static_cast<float>(rightPixelX) - ctx.cameraPosition.x,
                                  static_cast<float>(bottomPixelY) - ctx.cameraPosition.y);

            DrawCrossMarker(ctx.renderer, anchorLeft, markerSize, anchorColor);
            DrawCrossMarker(ctx.renderer, anchorRight, markerSize, anchorColor);
        }
    }
}

void Editor::RenderNoProjectionAnchorsImpl(const EditorContext& ctx)
{
    if (!m_ShowNoProjectionAnchors)
        return;

    // Check if 3D perspective is enabled
    auto perspState = ctx.renderer.GetPerspectiveState();
    bool is3DMode = perspState.enabled;

    int tileWidth = ctx.tilemap.GetTileWidth();
    int tileHeight = ctx.tilemap.GetTileHeight();

    EnsureNoProjBoundsCache(ctx);

    float markerSize = 6.0f;
    glm::vec4 anchorColor(0.0f, 1.0f, 0.0f, 1.0f);

    for (const auto& bounds : m_CachedNoProjBounds)
    {
        // Calculate anchor positions in world pixels
        int leftPixelX = bounds.minX * tileWidth;
        int rightPixelX = (bounds.maxX + 1) * tileWidth;
        int bottomPixelY = (bounds.maxY + 1) * tileHeight;

        // Screen-space positions
        glm::vec2 screenLeft(static_cast<float>(leftPixelX) - ctx.cameraPosition.x,
                             static_cast<float>(bottomPixelY) - ctx.cameraPosition.y);
        glm::vec2 screenRight(static_cast<float>(rightPixelX) - ctx.cameraPosition.x,
                              static_cast<float>(bottomPixelY) - ctx.cameraPosition.y);

        // Skip anchors behind the sphere in globe mode
        if (ctx.renderer.IsPointBehindSphere(screenLeft) &&
            ctx.renderer.IsPointBehindSphere(screenRight))
            continue;

        // In 3D mode, project through perspective
        glm::vec2 anchorLeft = is3DMode ? ctx.renderer.ProjectPoint(screenLeft) : screenLeft;
        glm::vec2 anchorRight = is3DMode ? ctx.renderer.ProjectPoint(screenRight) : screenRight;

        // Bottom-left anchor (only if visible)
        if (!ctx.renderer.IsPointBehindSphere(screenLeft))
            DrawCrossMarker(ctx.renderer, anchorLeft, markerSize, anchorColor);

        // Bottom-right anchor (only if visible)
        if (!ctx.renderer.IsPointBehindSphere(screenRight))
            DrawCrossMarker(ctx.renderer, anchorRight, markerSize, anchorColor);
    }

    // Draw manually defined structure anchors (cyan to distinguish from auto-detected green)
    const auto& structures = ctx.tilemap.GetNoProjectionStructures();
    float defMarkerSize = 6.0f;
    glm::vec4 definedAnchorColor(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan
    for (const auto& s : structures)
    {
        // Screen-space positions from world coordinates
        glm::vec2 screenLeft(s.leftAnchor.x - ctx.cameraPosition.x,
                             s.leftAnchor.y - ctx.cameraPosition.y);
        glm::vec2 screenRight(s.rightAnchor.x - ctx.cameraPosition.x,
                              s.rightAnchor.y - ctx.cameraPosition.y);

        // Skip anchors behind the sphere in globe mode
        if (ctx.renderer.IsPointBehindSphere(screenLeft) &&
            ctx.renderer.IsPointBehindSphere(screenRight))
            continue;

        // In 3D mode, project through perspective
        glm::vec2 anchorLeft = is3DMode ? ctx.renderer.ProjectPoint(screenLeft) : screenLeft;
        glm::vec2 anchorRight = is3DMode ? ctx.renderer.ProjectPoint(screenRight) : screenRight;

        // Left anchor cross (only if visible)
        if (!ctx.renderer.IsPointBehindSphere(screenLeft))
            DrawCrossMarker(ctx.renderer, anchorLeft, defMarkerSize, definedAnchorColor);

        // Right anchor cross (only if visible)
        if (!ctx.renderer.IsPointBehindSphere(screenRight))
            DrawCrossMarker(ctx.renderer, anchorRight, defMarkerSize, definedAnchorColor);

        // Stepped base line between anchors showing per-column effective base
        if (!ctx.renderer.IsPointBehindSphere(screenLeft) &&
            !ctx.renderer.IsPointBehindSphere(screenRight))
        {
            // Compute structure bounds across all layers
            int mapW = ctx.tilemap.GetMapWidth();
            int mapH = ctx.tilemap.GetMapHeight();
            size_t layerCount = ctx.tilemap.GetLayerCount();
            int sMinX = mapW, sMaxX = 0, sMinY = mapH, sMaxY = 0;
            for (int sy = 0; sy < mapH; ++sy)
            {
                for (int sx = 0; sx < mapW; ++sx)
                {
                    for (size_t li = 0; li < layerCount; ++li)
                    {
                        if (!ctx.tilemap.GetLayerNoProjection(sx, sy, li))
                            continue;
                        if (ctx.tilemap.GetTileStructureId(sx, sy, static_cast<int>(li) + 1) !=
                            s.id)
                            continue;
                        sMinX = std::min(sMinX, sx);
                        sMaxX = std::max(sMaxX, sx);
                        sMinY = std::min(sMinY, sy);
                        sMaxY = std::max(sMaxY, sy);
                        break;
                    }
                }
            }

            if (sMinX <= sMaxX)
            {
                int cols = sMaxX - sMinX + 1;
                float tw = static_cast<float>(tileWidth);
                float th = static_cast<float>(tileHeight);
                float anchorBaseY =
                    static_cast<float>((sMaxY + 1) * tileHeight) - ctx.cameraPosition.y;
                glm::vec4 lineColor(0.0f, 1.0f, 1.0f, 0.5f);

                // Compute per-column effective maxY (lowest non-transparent tile)
                std::vector<int> colEffMaxY(cols, sMinY - 1);
                for (int col = 0; col < cols; ++col)
                {
                    int tileX = sMinX + col;
                    for (int tileY = sMaxY; tileY >= sMinY; --tileY)
                    {
                        for (size_t li = 0; li < layerCount; ++li)
                        {
                            if (!ctx.tilemap.GetLayerNoProjection(tileX, tileY, li))
                                continue;
                            if (ctx.tilemap.GetTileStructureId(
                                    tileX, tileY, static_cast<int>(li) + 1) != s.id)
                                continue;
                            int tid = ctx.tilemap.GetLayerTile(tileX, tileY, li);
                            if (tid >= 0 && !ctx.tilemap.IsTileTransparent(tid))
                            {
                                colEffMaxY[col] = std::max(colEffMaxY[col], tileY);
                            }
                            break;
                        }
                    }
                }

                // Draw stepped base line
                float prevBaseY = -1.0f;
                for (int col = 0; col < cols; ++col)
                {
                    int effMaxY = colEffMaxY[col];
                    if (effMaxY < sMinY)
                        continue;
                    float colBaseY = anchorBaseY - static_cast<float>(sMaxY - effMaxY) * th;
                    float colX = static_cast<float>(sMinX + col) * tw - ctx.cameraPosition.x;

                    glm::vec2 hScreen(colX, colBaseY - 1.0f);
                    glm::vec2 hSize(tw, 2.0f);
                    if (is3DMode)
                    {
                        glm::vec2 p0 = ctx.renderer.ProjectPoint(hScreen);
                        glm::vec2 p1 =
                            ctx.renderer.ProjectPoint(glm::vec2(colX + tw, colBaseY - 1.0f));
                        hScreen = p0;
                        hSize = glm::vec2(p1.x - p0.x, 2.0f);
                    }
                    ctx.renderer.DrawColoredRect(hScreen, hSize, lineColor);

                    // Vertical connector between adjacent columns with different base
                    if (prevBaseY >= 0.0f && prevBaseY != colBaseY)
                    {
                        float vx = colX;
                        float vy = std::min(prevBaseY, colBaseY) - 1.0f;
                        float vh = std::abs(prevBaseY - colBaseY);
                        glm::vec2 vScreen(vx, vy);
                        glm::vec2 vSize(2.0f, vh);
                        if (is3DMode)
                        {
                            glm::vec2 pv = ctx.renderer.ProjectPoint(vScreen);
                            glm::vec2 pv2 = ctx.renderer.ProjectPoint(glm::vec2(vx, vy + vh));
                            vScreen = pv;
                            vSize = glm::vec2(2.0f, pv2.y - pv.y);
                        }
                        ctx.renderer.DrawColoredRect(vScreen, vSize, lineColor);
                    }
                    prevBaseY = colBaseY;
                }
            }
        }
    }
}

void Editor::RenderStructureOverlays(const EditorContext& ctx)
{
    if (!m_StructureEditMode)
        return;

    auto vr = CalcVisibleTileRange(ctx);

    // Draw tiles assigned to structures with purple overlay
    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            int structId = ctx.tilemap.GetTileStructureId(x, y, m_CurrentLayer + 1);
            if (structId >= 0)
            {
                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);

                float alpha = (structId == m_CurrentStructureId) ? 0.6f : 0.3f;
                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    glm::vec4(0.7f, 0.2f, 0.9f, alpha));
            }
        }
    }

    // Draw defined structure anchors (same style as debug overlay)
    // Anchors are stored in world coordinates
    float markerSize = 6.0f;
    const auto& structures = ctx.tilemap.GetNoProjectionStructures();
    for (const auto& s : structures)
    {
        glm::vec2 leftPos(s.leftAnchor.x - ctx.cameraPosition.x,
                          s.leftAnchor.y - ctx.cameraPosition.y);
        glm::vec2 rightPos(s.rightAnchor.x - ctx.cameraPosition.x,
                           s.rightAnchor.y - ctx.cameraPosition.y);

        // Green for normal, cyan for selected structure
        glm::vec4 anchorColor = (s.id == m_CurrentStructureId) ? glm::vec4(0.0f, 1.0f, 1.0f, 1.0f)
                                                               : glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        // Left anchor cross
        DrawCrossMarker(ctx.renderer, leftPos, markerSize, anchorColor);

        // Right anchor cross
        DrawCrossMarker(ctx.renderer, rightPos, markerSize, anchorColor);

        // Draw stepped base line showing per-column effective base
        {
            int mapW = ctx.tilemap.GetMapWidth();
            int mapH = ctx.tilemap.GetMapHeight();
            size_t layerCount = ctx.tilemap.GetLayerCount();
            int sMinX = mapW, sMaxX = 0, sMinY = mapH, sMaxY = 0;
            for (int sy = 0; sy < mapH; ++sy)
            {
                for (int sx = 0; sx < mapW; ++sx)
                {
                    for (size_t li = 0; li < layerCount; ++li)
                    {
                        if (!ctx.tilemap.GetLayerNoProjection(sx, sy, li))
                            continue;
                        if (ctx.tilemap.GetTileStructureId(sx, sy, static_cast<int>(li) + 1) !=
                            s.id)
                            continue;
                        sMinX = std::min(sMinX, sx);
                        sMaxX = std::max(sMaxX, sx);
                        sMinY = std::min(sMinY, sy);
                        sMaxY = std::max(sMaxY, sy);
                        break;
                    }
                }
            }

            if (sMinX <= sMaxX)
            {
                int cols = sMaxX - sMinX + 1;
                float tw = static_cast<float>(vr.tileWidth);
                float th = static_cast<float>(vr.tileHeight);
                float anchorBaseY =
                    static_cast<float>((sMaxY + 1) * vr.tileHeight) - ctx.cameraPosition.y;
                glm::vec4 lineColor(anchorColor.r, anchorColor.g, anchorColor.b, 0.5f);

                // Compute per-column effective maxY (lowest non-transparent tile)
                std::vector<int> colEffMaxY(cols, sMinY - 1);
                for (int col = 0; col < cols; ++col)
                {
                    int tileX = sMinX + col;
                    for (int tileY = sMaxY; tileY >= sMinY; --tileY)
                    {
                        for (size_t li = 0; li < layerCount; ++li)
                        {
                            if (!ctx.tilemap.GetLayerNoProjection(tileX, tileY, li))
                                continue;
                            if (ctx.tilemap.GetTileStructureId(
                                    tileX, tileY, static_cast<int>(li) + 1) != s.id)
                                continue;
                            int tid = ctx.tilemap.GetLayerTile(tileX, tileY, li);
                            if (tid >= 0 && !ctx.tilemap.IsTileTransparent(tid))
                            {
                                colEffMaxY[col] = std::max(colEffMaxY[col], tileY);
                            }
                            break;
                        }
                    }
                }

                // Draw stepped base line
                float prevBaseY = -1.0f;
                for (int col = 0; col < cols; ++col)
                {
                    int effMaxY = colEffMaxY[col];
                    if (effMaxY < sMinY)
                        continue;
                    float colBaseY = anchorBaseY - static_cast<float>(sMaxY - effMaxY) * th;
                    float colX = static_cast<float>(sMinX + col) * tw - ctx.cameraPosition.x;

                    ctx.renderer.DrawColoredRect(
                        glm::vec2(colX, colBaseY - 1.0f), glm::vec2(tw, 2.0f), lineColor);

                    // Vertical connector between adjacent columns with different base
                    if (prevBaseY >= 0.0f && prevBaseY != colBaseY)
                    {
                        float vy = std::min(prevBaseY, colBaseY) - 1.0f;
                        float vh = std::abs(prevBaseY - colBaseY);
                        ctx.renderer.DrawColoredRect(
                            glm::vec2(colX, vy), glm::vec2(2.0f, vh), lineColor);
                    }
                    prevBaseY = colBaseY;
                }
            }
        }
    }

    // Draw temporary anchors being placed (yellow, same style)
    if (m_TempLeftAnchor.x >= 0)
    {
        glm::vec2 pos(m_TempLeftAnchor.x - ctx.cameraPosition.x,
                      m_TempLeftAnchor.y - ctx.cameraPosition.y);
        glm::vec4 color(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
        DrawCrossMarker(ctx.renderer, pos, markerSize, color);
    }

    if (m_TempRightAnchor.x >= 0)
    {
        glm::vec2 pos(m_TempRightAnchor.x - ctx.cameraPosition.x,
                      m_TempRightAnchor.y - ctx.cameraPosition.y);
        glm::vec4 color(1.0f, 0.8f, 0.0f, 1.0f);  // Orange-yellow
        DrawCrossMarker(ctx.renderer, pos, markerSize, color);
    }
}

void Editor::RenderLayerFlagOverlays(const EditorContext& ctx,
                                     bool editMode,
                                     bool (Tilemap::*getter)(int, int, size_t) const,
                                     const glm::vec3& color)
{
    auto vr = CalcVisibleTileRange(ctx);

    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            if (editMode)
            {
                if (!(ctx.tilemap.*getter)(x, y, m_CurrentLayer))
                    continue;

                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);

                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    glm::vec4(color, 0.5f));
            }
            else
            {
                int count = 0;
                size_t layerCount = ctx.tilemap.GetLayerCount();
                for (size_t layer = 0; layer < layerCount; ++layer)
                {
                    if ((ctx.tilemap.*getter)(x, y, layer))
                        count++;
                }

                if (count == 0)
                    continue;

                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);

                float alpha =
                    0.15f + (static_cast<float>(count) / static_cast<float>(layerCount)) * 0.35f;

                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    glm::vec4(color, alpha));
            }
        }
    }
}

void Editor::RenderYSortPlusOverlays(const EditorContext& ctx)
{
    RenderLayerFlagOverlays(
        ctx, m_YSortPlusEditMode, &Tilemap::GetLayerYSortPlus, glm::vec3(0.0f, 0.8f, 0.8f));
}

void Editor::RenderYSortMinusOverlays(const EditorContext& ctx)
{
    RenderLayerFlagOverlays(
        ctx, m_YSortMinusEditMode, &Tilemap::GetLayerYSortMinus, glm::vec3(0.9f, 0.2f, 0.9f));
}

void Editor::RenderParticleZoneOverlays(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);
    float worldWidth = vr.screenSize.x;
    float worldHeight = vr.screenSize.y;

    const auto* zones = ctx.tilemap.GetParticleZones();
    if (!zones)
        return;

    for (const auto& zone : *zones)
    {
        // Calculate screen position
        glm::vec2 screenPos = zone.position - ctx.cameraPosition;

        // Cull zones outside view
        if (screenPos.x + zone.size.x < 0 || screenPos.x > worldWidth ||
            screenPos.y + zone.size.y < 0 || screenPos.y > worldHeight)
            continue;

        // Color based on particle type
        glm::vec4 color = GetParticleTypeColor(zone.type, 0.3f);

        if (!zone.enabled)
            color.a *= 0.3f;  // Dimmer if disabled

        ctx.renderer.DrawColoredRect(screenPos, zone.size, color);

        // Draw border for clarity
        float borderWidth = 2.0f;
        glm::vec4 borderColor = color;
        borderColor.a = 0.6f;

        // Top border
        ctx.renderer.DrawColoredRect(screenPos, glm::vec2(zone.size.x, borderWidth), borderColor);
        // Bottom border
        ctx.renderer.DrawColoredRect(
            glm::vec2(screenPos.x, screenPos.y + zone.size.y - borderWidth),
            glm::vec2(zone.size.x, borderWidth),
            borderColor);
        // Left border
        ctx.renderer.DrawColoredRect(screenPos, glm::vec2(borderWidth, zone.size.y), borderColor);
        // Right border
        ctx.renderer.DrawColoredRect(
            glm::vec2(screenPos.x + zone.size.x - borderWidth, screenPos.y),
            glm::vec2(borderWidth, zone.size.y),
            borderColor);
    }

    // Draw preview of zone being placed
    if (m_PlacingParticleZone)
    {
        // Get current mouse position in world coordinates
        double mouseX, mouseY;
        glfwGetCursorPos(ctx.window, &mouseX, &mouseY);

        float worldX =
            (static_cast<float>(mouseX) / static_cast<float>(ctx.screenWidth)) * worldWidth +
            ctx.cameraPosition.x;
        float worldY =
            (static_cast<float>(mouseY) / static_cast<float>(ctx.screenHeight)) * worldHeight +
            ctx.cameraPosition.y;

        auto zr = CalculateParticleZoneRect(
            worldX, worldY, ctx.tilemap.GetTileWidth(), ctx.tilemap.GetTileHeight());

        // Preview color based on type
        glm::vec4 previewColor = GetParticleTypeColor(m_CurrentParticleType, 0.5f);

        glm::vec2 previewPos(zr.x - ctx.cameraPosition.x, zr.y - ctx.cameraPosition.y);
        ctx.renderer.DrawColoredRect(previewPos, glm::vec2(zr.w, zr.h), previewColor);
    }
}

void Editor::RenderNPCDebugInfo(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);

    const float NPC_HITBOX_SIZE = PlayerCharacter::HITBOX_HEIGHT;

    for (const auto& npc : ctx.npcs)
    {
        glm::vec2 npcAnchor = npc.GetPosition();

        glm::vec2 npcHitboxPos(npcAnchor.x - NPC_HITBOX_SIZE * 0.5f - ctx.cameraPosition.x,
                               npcAnchor.y - NPC_HITBOX_SIZE - ctx.cameraPosition.y);
        glm::vec2 npcHitboxSize(NPC_HITBOX_SIZE, NPC_HITBOX_SIZE);

        if (npcHitboxPos.x + npcHitboxSize.x >= 0 && npcHitboxPos.x <= vr.screenSize.x &&
            npcHitboxPos.y + npcHitboxSize.y >= 0 && npcHitboxPos.y <= vr.screenSize.y)
        {
            ctx.renderer.DrawColoredRect(
                npcHitboxPos, npcHitboxSize, glm::vec4(1.0f, 0.0f, 1.0f, 0.3f));
        }

        int targetX = npc.m_TargetTileX;
        int targetY = npc.m_TargetTileY;

        glm::vec2 targetPos(targetX * vr.tileWidth - ctx.cameraPosition.x + vr.tileWidth * 0.5f,
                            targetY * vr.tileHeight - ctx.cameraPosition.y + vr.tileHeight * 0.5f);

        if (targetPos.x >= -vr.tileWidth && targetPos.x <= vr.screenSize.x + vr.tileWidth &&
            targetPos.y >= -vr.tileHeight && targetPos.y <= vr.screenSize.y + vr.tileHeight)
        {
            float dotSize = 6.0f;
            ctx.renderer.DrawColoredRect(targetPos - glm::vec2(dotSize * 0.5f),
                                         glm::vec2(dotSize),
                                         glm::vec4(0.0f, 1.0f, 0.0f, 0.8f));
        }
    }
}

void Editor::RenderCornerCuttingOverlays(const EditorContext& ctx)
{
    auto vr = CalcVisibleTileRange(ctx);

    // Player hitbox 16x16 pixels
    const float HITBOX_SIZE = PlayerCharacter::HITBOX_WIDTH;
    const float HITBOX_HALF = HITBOX_SIZE * 0.5f;  // 8 pixels from center
    const float TILE_SIZE = ctx.tilemap.GetTileWidth();

    // Walking allows 20% overlap threshold on diagonal corners only
    const float CORNER_OVERLAP_THRESHOLD = 0.20f;
    const float HITBOX_AREA = HITBOX_SIZE * HITBOX_SIZE;                    // 256 sq pixels
    const float MAX_OVERLAP_AREA = HITBOX_AREA * CORNER_OVERLAP_THRESHOLD;  // 51.2 sq pixels
    float walkingCornerPenetration = std::sqrt(MAX_OVERLAP_AREA);           // ~7.155 pixels

    // Running allows center-point collision penetration up to hitbox edge
    float runningEdgePenetration = HITBOX_HALF;  // 8 pixels

    // Render collision tolerance zones for all collision tiles
    for (int y = vr.startY; y < vr.endY; ++y)
    {
        for (int x = vr.startX; x < vr.endX; ++x)
        {
            if (!ctx.tilemap.GetTileCollision(x, y))
                continue;

            glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                              y * vr.tileHeight - ctx.cameraPosition.y);

            // Check adjacency for this tile to determine valid exposed corners and edges
            bool freeLeft = (x > 0) && !ctx.tilemap.GetTileCollision(x - 1, y);
            bool freeRight =
                (x < ctx.tilemap.GetMapWidth() - 1) && !ctx.tilemap.GetTileCollision(x + 1, y);
            bool freeTop = (y > 0) && !ctx.tilemap.GetTileCollision(x, y - 1);
            bool freeBottom =
                (y < ctx.tilemap.GetMapHeight() - 1) && !ctx.tilemap.GetTileCollision(x, y + 1);

            // Left Edge
            if (freeLeft)
            {
                ctx.renderer.DrawColoredRect(glm::vec2(tilePos.x, tilePos.y),
                                             glm::vec2(runningEdgePenetration, TILE_SIZE),
                                             glm::vec4(1.0f, 0.6f, 0.2f, 0.5f));
            }
            // Right Edge
            if (freeRight)
            {
                ctx.renderer.DrawColoredRect(
                    glm::vec2(tilePos.x + TILE_SIZE - runningEdgePenetration, tilePos.y),
                    glm::vec2(runningEdgePenetration, TILE_SIZE),
                    glm::vec4(1.0f, 0.6f, 0.2f, 0.5f));
            }
            // Top Edge
            if (freeTop)
            {
                ctx.renderer.DrawColoredRect(glm::vec2(tilePos.x, tilePos.y),
                                             glm::vec2(TILE_SIZE, runningEdgePenetration),
                                             glm::vec4(1.0f, 0.6f, 0.2f, 0.5f));
            }
            // Bottom Edge
            if (freeBottom)
            {
                ctx.renderer.DrawColoredRect(
                    glm::vec2(tilePos.x, tilePos.y + TILE_SIZE - runningEdgePenetration),
                    glm::vec2(TILE_SIZE, runningEdgePenetration),
                    glm::vec4(1.0f, 0.6f, 0.2f, 0.5f));
            }

            struct CornerInfo
            {
                int dx, dy;                  // Diagonal direction to check
                float x, y;                  // Screen position of overlap zone
                bool isValid;                // Is this a valid exposed corner?
                Tilemap::Corner cornerEnum;  // Which corner this is
            };

            // Check which corners have cutting blocked
            bool tlBlocked = ctx.tilemap.IsCornerCutBlocked(x, y, Tilemap::CORNER_TL);
            bool trBlocked = ctx.tilemap.IsCornerCutBlocked(x, y, Tilemap::CORNER_TR);
            bool blBlocked = ctx.tilemap.IsCornerCutBlocked(x, y, Tilemap::CORNER_BL);
            bool brBlocked = ctx.tilemap.IsCornerCutBlocked(x, y, Tilemap::CORNER_BR);

            CornerInfo corners[4] = {
                // Top-Left: Valid if Left & Top are free
                {-1, -1, tilePos.x, tilePos.y, freeLeft && freeTop, Tilemap::CORNER_TL},
                // Top-Right: Valid if Right & Top are free
                {1, -1, tilePos.x + TILE_SIZE, tilePos.y, freeRight && freeTop, Tilemap::CORNER_TR},
                // Bottom-Left: Valid if Left & Bottom are free
                {-1,
                 1,
                 tilePos.x,
                 tilePos.y + TILE_SIZE,
                 freeLeft && freeBottom,
                 Tilemap::CORNER_BL},
                // Bottom-Right: Valid if Right & Bottom are free
                {1,
                 1,
                 tilePos.x + TILE_SIZE,
                 tilePos.y + TILE_SIZE,
                 freeRight && freeBottom,
                 Tilemap::CORNER_BR}};

            bool cornerBlocked[4] = {tlBlocked, trBlocked, blBlocked, brBlocked};

            for (int i = 0; i < 4; ++i)
            {
                const auto& corner = corners[i];

                // Straight walls and internal corners have strictly no penetration
                if (!corner.isValid)
                    continue;

                int nx = x + corner.dx;
                int ny = y + corner.dy;

                // Only render if diagonal neighbor is walkable otherwise no escape path
                if (nx >= 0 && ny >= 0 && nx < ctx.tilemap.GetMapWidth() &&
                    ny < ctx.tilemap.GetMapHeight() && !ctx.tilemap.GetTileCollision(nx, ny))
                {
                    // Calculate positions based on corner direction
                    float walkX =
                        (corner.dx == -1) ? corner.x : corner.x - walkingCornerPenetration;
                    float walkY =
                        (corner.dy == -1) ? corner.y : corner.y - walkingCornerPenetration;

                    if (cornerBlocked[i])
                    {
                        // Draw red indicator for blocked corner cutting
                        ctx.renderer.DrawColoredRect(
                            glm::vec2(walkX, walkY),
                            glm::vec2(walkingCornerPenetration, walkingCornerPenetration),
                            glm::vec4(1.0f, 0.2f, 0.2f, 0.9f));
                    }
                    else
                    {
                        // Draw green walking corner penetration zone (normal)
                        ctx.renderer.DrawColoredRect(
                            glm::vec2(walkX, walkY),
                            glm::vec2(walkingCornerPenetration, walkingCornerPenetration),
                            glm::vec4(0.5f, 1.0f, 0.0f, 0.8f));
                    }
                }
            }
        }
    }
}

void Editor::RenderLayerOverlay(const EditorContext& ctx, int layerIndex, const glm::vec4& color)
{
    auto vr = CalcVisibleTileRange(ctx);

    for (int y = vr.startY; y < vr.endY; ++y)
        for (int x = vr.startX; x < vr.endX; ++x)
            if (ctx.tilemap.GetLayerTile(x, y, layerIndex) >= 0)
            {
                glm::vec2 tilePos(x * vr.tileWidth - ctx.cameraPosition.x,
                                  y * vr.tileHeight - ctx.cameraPosition.y);
                ctx.renderer.DrawColoredRect(
                    tilePos,
                    glm::vec2(static_cast<float>(vr.tileWidth), static_cast<float>(vr.tileHeight)),
                    color);
            }
}

void Editor::RenderEditorUI(const EditorContext& ctx)
{
    // Set tile picker projection and use base world dimensions without camera zoom
    float tilePickerWorldWidth =
        static_cast<float>(ctx.tilesVisibleWidth * ctx.tilemap.GetTileWidth());
    float tilePickerWorldHeight =
        static_cast<float>(ctx.tilesVisibleHeight * ctx.tilemap.GetTileHeight());
    glm::mat4 tilePickerProjection =
        glm::ortho(0.0f, tilePickerWorldWidth, tilePickerWorldHeight, 0.0f, -1.0f, 1.0f);
    ctx.renderer.SetProjection(tilePickerProjection);

    int dataTilesPerRow = ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();
    int dataTilesPerCol = ctx.tilemap.GetTilesetDataHeight() / ctx.tilemap.GetTileHeight();
    int totalTiles = dataTilesPerRow * dataTilesPerCol;

    int tilesPerRow = dataTilesPerRow;

    float baseTileSizePixels =
        (static_cast<float>(ctx.screenWidth) / static_cast<float>(tilesPerRow)) * 1.5f;
    float tileSizePixels = baseTileSizePixels * m_TilePickerZoom;

    float worldWidth = tilePickerWorldWidth;
    float worldHeight = tilePickerWorldHeight;

    float tilePickerWidth = static_cast<float>(ctx.screenWidth);
    float tilePickerHeight = static_cast<float>(ctx.screenHeight);

    float worldPickerWidth = (tilePickerWidth / ctx.screenWidth) * worldWidth;
    float worldPickerHeight = (tilePickerHeight / ctx.screenHeight) * worldHeight;

    // Background
    ctx.renderer.DrawColoredRect(glm::vec2(0.0f, 0.0f),
                                 glm::vec2(worldPickerWidth, worldPickerHeight),
                                 glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // Render only visible tiles, cull off-screen tiles
    int startCol =
        std::max(0, static_cast<int>(std::floor((-m_TilePickerOffsetX) / tileSizePixels)));
    int endCol = std::min(
        tilesPerRow - 1,
        static_cast<int>(std::floor((ctx.screenWidth - m_TilePickerOffsetX) / tileSizePixels)));
    int startRow =
        std::max(0, static_cast<int>(std::floor((-m_TilePickerOffsetY) / tileSizePixels)));
    int endRow = std::min(
        dataTilesPerCol - 1,
        static_cast<int>(std::floor((ctx.screenHeight - m_TilePickerOffsetY) / tileSizePixels)));

    for (int row = startRow; row <= endRow; ++row)
    {
        for (int col = startCol; col <= endCol; ++col)
        {
            int tileID = row * tilesPerRow + col;
            if (tileID < 0 || tileID >= totalTiles)
                continue;

            if (ctx.tilemap.IsTileTransparent(tileID))
                continue;

            float screenX = col * tileSizePixels + m_TilePickerOffsetX;
            float screenY = row * tileSizePixels + m_TilePickerOffsetY;

            float worldX = (screenX / ctx.screenWidth) * worldWidth;
            float worldY = (screenY / ctx.screenHeight) * worldHeight;
            float worldTileSize = (tileSizePixels / ctx.screenWidth) * worldWidth;

            int tilesetX = col * ctx.tilemap.GetTileWidth();
            int tilesetY = row * ctx.tilemap.GetTileHeight();

            glm::vec2 texCoord(static_cast<float>(tilesetX), static_cast<float>(tilesetY));
            glm::vec2 texSize(ctx.tilemap.GetTileWidth(), ctx.tilemap.GetTileHeight());

            glm::vec3 color =
                (tileID == m_SelectedTileID) ? glm::vec3(1.5f, 1.5f, 1.0f) : glm::vec3(1.0f);

            // Query renderer at runtime for Y-flip (OpenGL=true, Vulkan=false)
            bool flipY = ctx.renderer.RequiresYFlip();

            ctx.renderer.DrawSpriteRegion(ctx.tilemap.GetTilesetTexture(),
                                          glm::vec2(worldX, worldY),
                                          glm::vec2(worldTileSize, worldTileSize),
                                          texCoord,
                                          texSize,
                                          0.0f,
                                          color,
                                          flipY);
        }
    }

    // Selection rectangle
    if (m_IsSelectingTiles && m_SelectionStartTileID >= 0)
    {
        int startTileID = m_SelectionStartTileID;
        int endTileID = m_SelectedTileID;

        int startX = startTileID % tilesPerRow;
        int startY = startTileID / tilesPerRow;
        int endX = endTileID % tilesPerRow;
        int endY = endTileID / tilesPerRow;

        int minX = std::min(startX, endX);
        int maxX = std::max(startX, endX);
        int minY = std::min(startY, endY);
        int maxY = std::max(startY, endY);

        float selStartX = minX * tileSizePixels + m_TilePickerOffsetX;
        float selStartY = minY * tileSizePixels + m_TilePickerOffsetY;
        float selWidth = (maxX - minX + 1) * tileSizePixels;
        float selHeight = (maxY - minY + 1) * tileSizePixels;

        float worldSelX = (selStartX / ctx.screenWidth) * worldWidth;
        float worldSelY = (selStartY / ctx.screenHeight) * worldHeight;
        float worldSelWidth = (selWidth / ctx.screenWidth) * worldWidth;
        float worldSelHeight = (selHeight / ctx.screenHeight) * worldHeight;

        float outlineThickness = 2.0f;
        // TODO: consider drawing selection overlay in screen space to avoid scaling thickness with
        // DPI/zoom. Top
        ctx.renderer.DrawColoredRect(glm::vec2(worldSelX, worldSelY),
                                     glm::vec2(worldSelWidth, outlineThickness),
                                     glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
        // Bottom
        ctx.renderer.DrawColoredRect(
            glm::vec2(worldSelX, worldSelY + worldSelHeight - outlineThickness),
            glm::vec2(worldSelWidth, outlineThickness),
            glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
        // Left
        ctx.renderer.DrawColoredRect(glm::vec2(worldSelX, worldSelY),
                                     glm::vec2(outlineThickness, worldSelHeight),
                                     glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
        // Right
        ctx.renderer.DrawColoredRect(
            glm::vec2(worldSelX + worldSelWidth - outlineThickness, worldSelY),
            glm::vec2(outlineThickness, worldSelHeight),
            glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
    }

    // Draw animation frame highlights in animation edit mode
    if (m_AnimationEditMode && !m_AnimationFrames.empty())
    {
        for (size_t i = 0; i < m_AnimationFrames.size(); ++i)
        {
            int frameID = m_AnimationFrames[i];
            int frameX = frameID % tilesPerRow;
            int frameY = frameID / tilesPerRow;

            float frameScreenX = frameX * tileSizePixels + m_TilePickerOffsetX;
            float frameScreenY = frameY * tileSizePixels + m_TilePickerOffsetY;

            float worldFrameX = (frameScreenX / ctx.screenWidth) * worldWidth;
            float worldFrameY = (frameScreenY / ctx.screenHeight) * worldHeight;
            float worldTileSize = (tileSizePixels / ctx.screenWidth) * worldWidth;

            // Draw numbered highlight
            float outlineThickness = 2.0f;
            glm::vec4 highlightColor(0.0f, 1.0f, 0.0f, 1.0f);
            ctx.renderer.DrawColoredRect(glm::vec2(worldFrameX, worldFrameY),
                                         glm::vec2(worldTileSize, outlineThickness),
                                         highlightColor);
            ctx.renderer.DrawColoredRect(
                glm::vec2(worldFrameX, worldFrameY + worldTileSize - outlineThickness),
                glm::vec2(worldTileSize, outlineThickness),
                highlightColor);
            ctx.renderer.DrawColoredRect(glm::vec2(worldFrameX, worldFrameY),
                                         glm::vec2(outlineThickness, worldTileSize),
                                         highlightColor);
            ctx.renderer.DrawColoredRect(
                glm::vec2(worldFrameX + worldTileSize - outlineThickness, worldFrameY),
                glm::vec2(outlineThickness, worldTileSize),
                highlightColor);

            // Draw frame number
            std::string frameNum = std::to_string(i + 1);
            ctx.renderer.DrawText(frameNum,
                                  glm::vec2(worldFrameX + 2.0f, worldFrameY + 2.0f),
                                  0.3f,
                                  glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    // Draw animation mode status
    if (m_AnimationEditMode)
    {
        std::string animStatus;
        if (m_SelectedAnimationId >= 0)
        {
            animStatus =
                "Animation tile: Click map to apply #" + std::to_string(m_SelectedAnimationId);
        }
        else if (!m_AnimationFrames.empty())
        {
            animStatus = "Animation tile: " + std::to_string(m_AnimationFrames.size()) +
                         " frames (" +
                         std::to_string(static_cast<int>(m_AnimationFrameDuration * 1000)) +
                         "ms) - Enter to create";
        }
        else
        {
            animStatus = "Animation tile: Click tiles to add frames";
        }
        ctx.renderer.DrawText(
            animStatus, glm::vec2(20.0f, 20.0f), 0.4f, glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

void Editor::RenderPlacementPreview(const EditorContext& ctx)
{
    // Draw animation mode status when not in tile picker
    if (m_AnimationEditMode && !m_ShowTilePicker && m_SelectedAnimationId >= 0)
    {
        std::string animStatus = "Animation tile: Click map to apply #" +
                                 std::to_string(m_SelectedAnimationId) +
                                 " (Esc to cancel, K to exit)";
        ctx.renderer.DrawText(
            animStatus, glm::vec2(20.0f, 20.0f), 0.4f, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Only show preview if we have a selection and are not in tile picker
    if (m_ShowTilePicker)
        return;
    if (m_SelectedTileStartID < 0)
        return;

    double mouseX, mouseY;
    glfwGetCursorPos(ctx.window, &mouseX, &mouseY);

    // Convert screen coordinates to world coordinates
    float baseWorldWidth = static_cast<float>(ctx.tilesVisibleWidth * ctx.tilemap.GetTileWidth());
    float baseWorldHeight =
        static_cast<float>(ctx.tilesVisibleHeight * ctx.tilemap.GetTileHeight());
    float zoom = std::max(ctx.cameraZoom, 0.01f);
    float worldWidth = baseWorldWidth / zoom;
    float worldHeight = baseWorldHeight / zoom;

    // Convert mouse position to world coordinates
    float worldX = (static_cast<float>(mouseX) / static_cast<float>(ctx.screenWidth)) * worldWidth +
                   ctx.cameraPosition.x;
    float worldY =
        (static_cast<float>(mouseY) / static_cast<float>(ctx.screenHeight)) * worldHeight +
        ctx.cameraPosition.y;

    // Convert world coordinates to tile coordinates
    int tileX = static_cast<int>(std::floor(worldX / ctx.tilemap.GetTileWidth()));
    int tileY = static_cast<int>(std::floor(worldY / ctx.tilemap.GetTileHeight()));

    int dataTilesPerRow = ctx.tilemap.GetTilesetDataWidth() / ctx.tilemap.GetTileWidth();
    int tileWidth = ctx.tilemap.GetTileWidth();
    int tileHeight = ctx.tilemap.GetTileHeight();

    if (m_MultiTileSelectionMode)
    {
        int rotatedWidth = (m_MultiTileRotation == 90 || m_MultiTileRotation == 270)
                               ? m_SelectedTileHeight
                               : m_SelectedTileWidth;
        int rotatedHeight = (m_MultiTileRotation == 90 || m_MultiTileRotation == 270)
                                ? m_SelectedTileWidth
                                : m_SelectedTileHeight;
        float tileRotation = GetCompensatedTileRotation();

        // Render preview of multi-tile selection with rotation
        for (int dy = 0; dy < rotatedHeight; ++dy)
        {
            for (int dx = 0; dx < rotatedWidth; ++dx)
            {
                int sourceDx, sourceDy;
                CalculateRotatedSourceTile(dx, dy, sourceDx, sourceDy);

                int previewX = tileX + dx;
                int previewY = tileY + dy;
                int sourceTileID = m_SelectedTileStartID + sourceDy * dataTilesPerRow + sourceDx;

                // Calculate tile position in camera-relative coordinates
                glm::vec2 tilePos((previewX * tileWidth) - ctx.cameraPosition.x,
                                  (previewY * tileHeight) - ctx.cameraPosition.y);

                // Render semi-transparent preview
                int tilesetX = (sourceTileID % dataTilesPerRow) * tileWidth;
                int tilesetY = (sourceTileID / dataTilesPerRow) * tileHeight;

                glm::vec2 texCoord(static_cast<float>(tilesetX), static_cast<float>(tilesetY));
                glm::vec2 texSize(ctx.tilemap.GetTileWidth(), ctx.tilemap.GetTileHeight());

                bool flipY = ctx.renderer.RequiresYFlip();

                ctx.renderer.DrawSpriteRegion(
                    ctx.tilemap.GetTilesetTexture(),
                    tilePos,
                    glm::vec2(ctx.tilemap.GetTileWidth(), ctx.tilemap.GetTileHeight()),
                    texCoord,
                    texSize,
                    tileRotation,
                    glm::vec3(1.0f, 1.0f, 0.5f),
                    flipY);

                // Render outline
                ctx.renderer.DrawColoredRect(tilePos,
                                             glm::vec2(static_cast<float>(tileWidth), 1.0f),
                                             glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Top
                ctx.renderer.DrawColoredRect(
                    glm::vec2(tilePos.x, tilePos.y + static_cast<float>(tileHeight) - 1.0f),
                    glm::vec2(static_cast<float>(tileWidth), 1.0f),
                    glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Bottom
                ctx.renderer.DrawColoredRect(tilePos,
                                             glm::vec2(1.0f, static_cast<float>(tileHeight)),
                                             glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Left
                ctx.renderer.DrawColoredRect(
                    glm::vec2(tilePos.x + static_cast<float>(tileWidth) - 1.0f, tilePos.y),
                    glm::vec2(1.0f, static_cast<float>(tileHeight)),
                    glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Right
            }
        }
    }
    else
    {
        // Render preview of single tile
        if (tileX >= 0 && tileX < ctx.tilemap.GetMapWidth() && tileY >= 0 &&
            tileY < ctx.tilemap.GetMapHeight())
        {
            glm::vec2 tilePos((tileX * tileWidth) - ctx.cameraPosition.x,
                              (tileY * tileHeight) - ctx.cameraPosition.y);

            int tilesetX = (m_SelectedTileStartID % dataTilesPerRow) * tileWidth;
            int tilesetY = (m_SelectedTileStartID / dataTilesPerRow) * tileHeight;

            glm::vec2 texCoord(static_cast<float>(tilesetX), static_cast<float>(tilesetY));
            glm::vec2 texSize(ctx.tilemap.GetTileWidth(), ctx.tilemap.GetTileHeight());

            // Query renderer at runtime for Y-flip (OpenGL=true, Vulkan=false)
            bool flipY = ctx.renderer.RequiresYFlip();

            float tileRotation = GetCompensatedTileRotation();

            ctx.renderer.DrawSpriteRegion(
                ctx.tilemap.GetTilesetTexture(),
                tilePos,
                glm::vec2(static_cast<float>(tileWidth), static_cast<float>(tileHeight)),
                texCoord,
                texSize,
                tileRotation,
                glm::vec3(1.0f, 1.0f, 0.5f),
                flipY);

            // Render outline
            ctx.renderer.DrawColoredRect(tilePos,
                                         glm::vec2(static_cast<float>(tileWidth), 1.0f),
                                         glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Top
            ctx.renderer.DrawColoredRect(
                glm::vec2(tilePos.x, tilePos.y + static_cast<float>(tileHeight) - 1.0f),
                glm::vec2(static_cast<float>(tileWidth), 1.0f),
                glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Bottom
            ctx.renderer.DrawColoredRect(tilePos,
                                         glm::vec2(1.0f, static_cast<float>(tileHeight)),
                                         glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Left
            ctx.renderer.DrawColoredRect(
                glm::vec2(tilePos.x + static_cast<float>(tileWidth) - 1.0f, tilePos.y),
                glm::vec2(1.0f, static_cast<float>(tileHeight)),
                glm::vec4(1.0f, 1.0f, 0.0f, 0.8f));  // Right
        }
    }
}
