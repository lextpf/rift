#include "DialogueManager.hpp"
#include "DialogueTypes.hpp"
#include "Game.hpp"

#include "AmbienceConfig.hpp"
#include "Appearance.hpp"
#include "CharacterConstants.hpp"
#include "NpcSprite.hpp"
#include "Transform.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
// Word-wrap text to fit within maxWidth. ASCII/space-delimited only;
// UTF-8 glyphs and very long tokens are not split.
template <typename MeasureFn>
std::vector<std::string> WrapText(const std::string& text, float maxWidth, MeasureFn measureWidth)
{
    std::vector<std::string> lines;
    std::string currentLine;
    std::string word;

    auto commitWord = [&]()
    {
        if (word.empty())
            return;
        const std::string testLine = currentLine.empty() ? word : (currentLine + " " + word);
        if (measureWidth(testLine) > maxWidth && !currentLine.empty())
        {
            lines.push_back(currentLine);
            currentLine = word;
        }
        else
        {
            currentLine = testLine;
        }
        word.clear();
    };

    auto commitLine = [&]()
    {
        if (!currentLine.empty())
        {
            lines.push_back(currentLine);
            currentLine.clear();
        }
    };

    for (char c : text)
    {
        if (c == ' ')
            commitWord();
        else if (c == '\n')
        {
            commitWord();
            commitLine();
        }
        else
            word += c;
    }
    commitWord();
    commitLine();

    // Break any line that still exceeds maxWidth (e.g., a single long word) by
    // splitting at the last character that fits.
    std::vector<std::string> result;
    for (auto& line : lines)
    {
        while (measureWidth(line) > maxWidth && line.size() > 1)
        {
            // Binary-search for the longest prefix that fits.
            size_t lo = 1;
            size_t hi = line.size();
            while (lo < hi)
            {
                size_t mid = lo + (hi - lo + 1) / 2;
                if (measureWidth(line.substr(0, mid)) <= maxWidth)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            result.push_back(line.substr(0, lo));
            line = line.substr(lo);
        }
        result.push_back(std::move(line));
    }

    return result;
}

void DrawRightArrow(IRenderer& renderer, float arrowX, float arrowCenterY, float z, glm::vec4 color)
{
    renderer.DrawColoredRect(
        glm::vec2(arrowX, arrowCenterY - 2.0f * z), glm::vec2(1.0f * z, 1.0f * z), color);
    renderer.DrawColoredRect(
        glm::vec2(arrowX, arrowCenterY - 1.0f * z), glm::vec2(2.0f * z, 1.0f * z), color);
    renderer.DrawColoredRect(glm::vec2(arrowX, arrowCenterY), glm::vec2(3.0f * z, 1.0f * z), color);
    renderer.DrawColoredRect(
        glm::vec2(arrowX, arrowCenterY + 1.0f * z), glm::vec2(2.0f * z, 1.0f * z), color);
    renderer.DrawColoredRect(
        glm::vec2(arrowX, arrowCenterY + 2.0f * z), glm::vec2(1.0f * z, 1.0f * z), color);
}

void DrawContinuePrompt(IRenderer& renderer,
                        float promptX,
                        float promptY,
                        float textScale,
                        float outlineSize,
                        float z,
                        float fade = 1.0f,
                        float time = 0.0f)
{
    const float promptScale = textScale * 0.85f;
    glm::vec3 promptColor(0.55f, 0.52f, 0.48f);
    renderer.DrawText("Continue",
                      glm::vec2(promptX, promptY),
                      promptScale,
                      promptColor,
                      outlineSize,
                      0.7f * fade);

    float promptAscent = renderer.GetTextAscent(promptScale);
    float arrowCenterY = promptY - promptAscent * 0.5f;
    float arrowX = promptX - 6.0f * z;
    DrawRightArrow(renderer, arrowX, arrowCenterY, z, glm::vec4(0.65f, 0.52f, 0.2f, 0.85f * fade));
}
// Filled rounded rect via pixel-stepped staircase corners (progressively wider
// horizontal strips approximate the radius).
void DrawFilledRoundedRect(
    IRenderer& renderer, glm::vec2 pos, glm::vec2 size, glm::vec4 color, float radius, float step)
{
    int steps = static_cast<int>(std::round(radius / step));

    // Top corners (narrowest to widest).
    for (int i = steps; i >= 1; --i)
    {
        float inset = static_cast<float>(i) * step;
        float y = pos.y + static_cast<float>(steps - i) * step;
        renderer.DrawColoredRect(
            glm::vec2(pos.x + inset, y), glm::vec2(size.x - 2 * inset, step), color);
    }

    // Bottom corners (mirrored).
    for (int i = 1; i <= steps; ++i)
    {
        float inset = static_cast<float>(i) * step;
        float y = pos.y + size.y - static_cast<float>(steps - i + 1) * step;
        renderer.DrawColoredRect(
            glm::vec2(pos.x + inset, y), glm::vec2(size.x - 2 * inset, step), color);
    }

    renderer.DrawColoredRect(
        glm::vec2(pos.x, pos.y + radius), glm::vec2(size.x, size.y - 2 * radius), color);
}

// Filled rounded rect with a vertical gradient (top -> bottom color).
void DrawFilledRoundedRectGradient(IRenderer& renderer,
                                   glm::vec2 pos,
                                   glm::vec2 size,
                                   glm::vec4 colorTop,
                                   glm::vec4 colorBot,
                                   float radius,
                                   float step)
{
    // Guard degenerate sizes (sub-pixel / negative-area strips when the box is tiny).
    if (size.y <= 0.0f || size.x <= 0.0f)
    {
        return;
    }

    constexpr int kStrips = 10;
    float bodyH = size.y - 2 * radius;
    if (bodyH < 0.0f)
    {
        bodyH = 0.0f;
    }
    float stripH = (kStrips > 0) ? bodyH / static_cast<float>(kStrips) : bodyH;
    int cornerSteps = static_cast<int>(std::round(radius / step));

    for (int i = cornerSteps; i >= 1; --i)
    {
        float inset = static_cast<float>(i) * step;
        float y = pos.y + static_cast<float>(cornerSteps - i) * step;
        float t = (y - pos.y) / size.y;
        glm::vec4 c = colorTop + (colorBot - colorTop) * t;
        renderer.DrawColoredRect(
            glm::vec2(pos.x + inset, y), glm::vec2(size.x - 2 * inset, step), c);
    }

    for (int s = 0; s < kStrips; ++s)
    {
        float y = pos.y + radius + static_cast<float>(s) * stripH;
        float t = (y - pos.y) / size.y;
        glm::vec4 c = colorTop + (colorBot - colorTop) * t;
        renderer.DrawColoredRect(glm::vec2(pos.x, y), glm::vec2(size.x, stripH), c);
    }

    for (int i = 1; i <= cornerSteps; ++i)
    {
        float inset = static_cast<float>(i) * step;
        float y = pos.y + size.y - static_cast<float>(cornerSteps - i + 1) * step;
        float t = (y - pos.y) / size.y;
        glm::vec4 c = colorTop + (colorBot - colorTop) * t;
        renderer.DrawColoredRect(
            glm::vec2(pos.x + inset, y), glm::vec2(size.x - 2 * inset, step), c);
    }
}

// Border outline of a rounded rect with pixel-stepped corners.
void DrawRoundedRectBorder(IRenderer& renderer,
                           glm::vec2 pos,
                           glm::vec2 size,
                           glm::vec4 color,
                           float radius,
                           float step,
                           float borderWidth)
{
    int steps = static_cast<int>(std::round(radius / step));
    float bw = borderWidth;

    // Horizontal edges (inset by radius).
    renderer.DrawColoredRect(
        glm::vec2(pos.x + radius, pos.y), glm::vec2(size.x - 2 * radius, bw), color);
    renderer.DrawColoredRect(
        glm::vec2(pos.x + radius, pos.y + size.y - bw), glm::vec2(size.x - 2 * radius, bw), color);

    // Vertical edges (inset by radius).
    renderer.DrawColoredRect(
        glm::vec2(pos.x, pos.y + radius), glm::vec2(bw, size.y - 2 * radius), color);
    renderer.DrawColoredRect(
        glm::vec2(pos.x + size.x - bw, pos.y + radius), glm::vec2(bw, size.y - 2 * radius), color);

    // Corner steps connecting the edges.
    for (int i = 1; i < steps; ++i)
    {
        float insetH = static_cast<float>(i) * step;
        float insetV = static_cast<float>(steps - i) * step;

        // Top-left
        renderer.DrawColoredRect(
            glm::vec2(pos.x + insetH, pos.y + insetV), glm::vec2(bw, step), color);
        // Top-right
        renderer.DrawColoredRect(
            glm::vec2(pos.x + size.x - insetH - bw, pos.y + insetV), glm::vec2(bw, step), color);
        // Bottom-left
        renderer.DrawColoredRect(
            glm::vec2(pos.x + insetH, pos.y + size.y - insetV - step), glm::vec2(bw, step), color);
        // Bottom-right
        renderer.DrawColoredRect(
            glm::vec2(pos.x + size.x - insetH - bw, pos.y + size.y - insetV - step),
            glm::vec2(bw, step),
            color);
    }
}

// Dialogue panel helpers (translucent slate + ribbon + selection triangle).

// Snap to the nearest screen pixel so border edges don't fringe at fractional zoom.
float SnapToPixel(float value, float z)
{
    if (z <= 0.0f)
        return value;
    return std::floor(value / z + 0.5f) * z;
}

// Translucent dark slate panel: single rgba fill + 1px lighter border.
// The fill's alpha lets the world show through so the panel harmonizes with
// whatever's behind it across day/night/grass/sky.
void DrawSlatePanel(IRenderer& renderer, glm::vec2 pos, glm::vec2 size, float z, float fadeAlpha)
{
    const float pxOne = 1.0f * z;
    const glm::vec4 fill(ambience::DIALOGUE_PANEL_FILL_RGB,
                         ambience::DIALOGUE_PANEL_FILL_ALPHA * fadeAlpha);
    const glm::vec4 border(ambience::DIALOGUE_PANEL_BORDER, fadeAlpha);

    renderer.DrawColoredRect(pos, size, fill);
    // 1px border on all four edges.
    renderer.DrawColoredRect(pos, glm::vec2(size.x, pxOne), border);
    renderer.DrawColoredRect(
        glm::vec2(pos.x, pos.y + size.y - pxOne), glm::vec2(size.x, pxOne), border);
    renderer.DrawColoredRect(pos, glm::vec2(pxOne, size.y), border);
    renderer.DrawColoredRect(
        glm::vec2(pos.x + size.x - pxOne, pos.y), glm::vec2(pxOne, size.y), border);
}

// Small > triangle (4x7 px) marking the selected option. Widths form a
// symmetric kite around the width-4 peak so the bottom edge closes cleanly.
void DrawAccentTriangle(IRenderer& renderer, glm::vec2 pos, glm::vec3 accent, float z, float alpha)
{
    static constexpr int widths[] = {1, 2, 3, 4, 3, 2, 1};  // 7-row right-pointing triangle.
    constexpr int rowCount = static_cast<int>(sizeof(widths) / sizeof(widths[0]));
    const float pxOne = 1.0f * z;
    const glm::vec4 c(accent, alpha);
    for (int row = 0; row < rowCount; ++row)
    {
        renderer.DrawColoredRect(glm::vec2(pos.x, pos.y + static_cast<float>(row) * pxOne),
                                 glm::vec2(static_cast<float>(widths[row]) * pxOne, pxOne),
                                 c);
    }
}

// Speaker name ribbon at the top-left of the dialogue panel. Background is
// the per-NPC accent; text is always cream + black outline (the hardcoded
// black outline defines text against any accent background).
void DrawSpeakerRibbon(IRenderer& renderer,
                       glm::vec2 anchorTopLeft,
                       const std::string& name,
                       glm::vec3 accent,
                       float z,
                       float fadeAlpha,
                       float textScale,
                       float outlineSize)
{
    if (name.empty())
        return;
    const float pad = ambience::DIALOGUE_RIBBON_PADDING_X * z;
    const float pxOne = 1.0f * z;
    const float ribbonH = ambience::DIALOGUE_RIBBON_HEIGHT * z;
    const float nameW = renderer.GetTextWidth(name, textScale);
    const float ribbonW = nameW + 2.0f * pad;

    const glm::vec4 bg(accent, 0.95f * fadeAlpha);
    const glm::vec4 border(accent * 0.7f, 0.6f * fadeAlpha);

    renderer.DrawColoredRect(anchorTopLeft, glm::vec2(ribbonW, ribbonH), bg);
    // 1px border for separation.
    renderer.DrawColoredRect(anchorTopLeft, glm::vec2(ribbonW, pxOne), border);
    renderer.DrawColoredRect(glm::vec2(anchorTopLeft.x, anchorTopLeft.y + ribbonH - pxOne),
                             glm::vec2(ribbonW, pxOne),
                             border);
    renderer.DrawColoredRect(anchorTopLeft, glm::vec2(pxOne, ribbonH), border);
    renderer.DrawColoredRect(glm::vec2(anchorTopLeft.x + ribbonW - pxOne, anchorTopLeft.y),
                             glm::vec2(pxOne, ribbonH),
                             border);

    // Light fill + black outline (renderer's outline is hardcoded black). The
    // cream fill stays readable across any accent ribbon background.
    const glm::vec3 textColor = ambience::DIALOGUE_RIBBON_TEXT_COLOR;

    const float ascent = renderer.GetTextAscent(textScale);
    const float textY = anchorTopLeft.y + (ribbonH - ascent) * 0.5f + ascent;
    renderer.DrawText(name,
                      glm::vec2(anchorTopLeft.x + pad, textY),
                      textScale,
                      textColor,
                      outlineSize,
                      fadeAlpha);
}
}  // namespace

bool Game::HasDialogueNPC() const
{
    return static_cast<bool>(FindNPCById(m_DialogueUi.npcId));
}

void Game::RenderNPCHeadText()
{
    if (!m_DialogueUi.inDialogue || m_DialogueUi.text.empty() || !HasDialogueNPC())
    {
        return;
    }

    glm::vec2 npcWorldPos = m_World.get<Transform>(FindNPCById(m_DialogueUi.npcId)).position;
    glm::vec2 npcScreenPos = npcWorldPos - m_Camera.GetState().position;

    // Place the text above the NPC's head.
    float textAreaWidth = 180.0f;
    const float NPC_SPRITE_HEIGHT = CharacterConstants::SPRITE_HEIGHT;
    float npcTopY = npcScreenPos.y - NPC_SPRITE_HEIGHT;
    float npcCenterX = npcScreenPos.x;

    glm::vec2 textAreaPos(npcCenterX - textAreaWidth * 0.5f, npcTopY - 10.0f);
    // TODO: scale this with zoom; also clamp to the visible screen so head
    // text cannot render off-screen.
    glm::vec2 textAreaSize(textAreaWidth, 50.0f);

    RenderDialogueText(textAreaPos, textAreaSize);
}

void Game::RenderDialogueText(glm::vec2 boxPos, glm::vec2 boxSize)
{
    if (m_DialogueUi.text.empty())
    {
        return;
    }

    float scale = 0.2f;
    float lineHeight = 6.0f;
    float maxWidth = boxSize.x - 20.0f;

    auto lines = WrapText(m_DialogueUi.text,
                          maxWidth,
                          [&](const std::string& s) { return m_Renderer->GetTextWidth(s, scale); });

    // Render each line, centered horizontally.
    float currentY = boxPos.y;
    glm::vec3 textColor(1.0f, 1.0f, 1.0f);

    float boxBottomY = boxPos.y + boxSize.y;
    for (const std::string& line : lines)
    {
        if (currentY + lineHeight > boxBottomY)
        {
            break;  // Stop rendering lines that would spill past the box.
        }
        if (!line.empty())
        {
            float lineWidth = m_Renderer->GetTextWidth(line, scale);
            float lineStartX = boxPos.x + (boxSize.x - lineWidth) * 0.5f;
            m_Renderer->DrawText(line, glm::vec2(lineStartX, currentY), scale, textColor);
        }
        currentY += lineHeight;
    }
}

void Game::RenderDialogueTreeBox()
{
    if (!m_DialogueManager.IsActive())
    {
        return;
    }

    const DialogueNode* node = m_DialogueManager.GetCurrentNode();
    if (!node)
    {
        return;
    }

    const float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    const float baseWorldHeight =
        static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    const float worldWidth = baseWorldWidth / m_Camera.GetState().zoom;
    const float worldHeight = baseWorldHeight / m_Camera.GetState().zoom;

    // Inverse-zoom so UI sizes stay constant on screen across zoom levels.
    const float z = 1.0f / m_Camera.GetState().zoom;

    // Fade-in animation (smoothstep over 0.2s).
    constexpr float kFadeDuration = 0.2f;
    const float fadeT = std::min(1.0f, m_DialogueUi.boxFadeTimer / kFadeDuration);
    const float fadeAlpha = fadeT * fadeT * (3.0f - 2.0f * fadeT);

    // Panel: 90% width, 60px height, bottom-anchored.
    float boxWidth = baseWorldWidth * 0.9f * z;
    float boxHeight = 60.0f * z;
    float boxX = (worldWidth - boxWidth) * 0.5f;
    float boxY = worldHeight - boxHeight - (10.0f * z);

    // Subtle scale-in (1.04 -> 1.00 during fade).
    {
        const float scaleFactor =
            ambience::DIALOGUE_BOX_SCALE_END +
            (ambience::DIALOGUE_BOX_SCALE_START - ambience::DIALOGUE_BOX_SCALE_END) *
                (1.0f - fadeAlpha);
        if (scaleFactor != 1.0f)
        {
            const float sw = boxWidth * scaleFactor;
            const float sh = boxHeight * scaleFactor;
            boxX -= (sw - boxWidth) * 0.5f;
            boxY -= (sh - boxHeight) * 0.5f;
            boxWidth = sw;
            boxHeight = sh;
        }
    }

    // Pixel-snap so the border doesn't fringe at fractional zoom levels.
    boxX = SnapToPixel(boxX, z);
    boxY = SnapToPixel(boxY, z);

    const glm::vec2 boxPos(boxX, boxY);
    const glm::vec2 boxSize(boxWidth, boxHeight);
    const float padInner = ambience::DIALOGUE_PANEL_PADDING * z;

    DrawSlatePanel(*m_Renderer, boxPos, boxSize, z, fadeAlpha);

    // Per-NPC accent for the speaker ribbon. Sampled lazily from the sprite
    // (or player sprite on player-turn lines); fallback gold otherwise.
    glm::vec3 accent = ambience::DIALOGUE_ACCENT_FALLBACK;
    const bool isPlayerTurn =
        !node->speaker.empty() && (node->speaker == "Player" || node->speaker == "You");
    if (isPlayerTurn)
    {
        accent = m_World.get<Appearance>(m_PlayerEntity).accentColor;
    }
    else if (HasDialogueNPC())
    {
        accent = m_World.get<NpcSprite>(FindNPCById(m_DialogueUi.npcId)).accentColor;
    }

    // Layout constants for the body text area.
    const float padding = 10.0f * z;
    const float textScale = 0.18f * z;
    const float lineHeight = 5.5f * z;
    // Ribbon and body use the same outline weight (renderer's outline is black).
    // With light fills (DIALOGUE_BODY_TEXT_COLOR / DIALOGUE_RIBBON_TEXT_COLOR)
    // this gives crisp outlined text against both the slate panel and the accent ribbon.
    const float ribbonOutlineSize = 2.0f;
    const float bodyOutlineSize = 2.0f;
    const float textAlpha = fadeAlpha;
    const float textAscent = m_Renderer->GetTextAscent(textScale);

    // Text area: full panel width minus inner padding.
    const float textAreaLeft = boxX + padInner;
    const float textAreaTop = boxY + padInner;
    const float textAreaRight = boxX + boxWidth - padInner;
    const float textAreaBottom = boxY + boxHeight - padInner;
    const float maxTextWidth = textAreaRight - textAreaLeft;

    // Speaker ribbon at the top of the text area; pushes body text below.
    float bodyTextStartY = textAreaTop + textAscent;
    float speakerHeightUsed = 0.0f;
    if (!node->speaker.empty())
    {
        const float ribbonScale = textScale * 1.2f;
        const float ribbonY = textAreaTop;
        const float ribbonX = textAreaLeft;
        DrawSpeakerRibbon(*m_Renderer,
                          glm::vec2(SnapToPixel(ribbonX, z), SnapToPixel(ribbonY, z)),
                          node->speaker,
                          accent,
                          z,
                          fadeAlpha,
                          ribbonScale,
                          ribbonOutlineSize);

        speakerHeightUsed = ambience::DIALOGUE_RIBBON_HEIGHT * z + 2.0f * z;
        bodyTextStartY = textAreaTop + speakerHeightUsed + textAscent;
    }
    const float availableHeight = textAreaBottom - (textAreaTop + speakerHeightUsed);

    auto allLines =
        WrapText(node->text,
                 maxTextWidth,
                 [&](const std::string& s) { return m_Renderer->GetTextWidth(s, textScale); });

    const auto& visibleOptions = m_DialogueManager.GetVisibleOptions();
    const int numOptions = static_cast<int>(visibleOptions.size());

    // Pagination math, measured against the text area's bounds.
    const int totalLines = static_cast<int>(allLines.size());
    const float optionsBottomPadding = 7.0f * z;
    float effectiveOptionsSpace =
        static_cast<float>(numOptions) * lineHeight - (padding - optionsBottomPadding);
    if (effectiveOptionsSpace < 0)
        effectiveOptionsSpace = 0;
    const float spaceForText = availableHeight - effectiveOptionsSpace;
    int maxTextLines = static_cast<int>(spaceForText / lineHeight) + 1;
    if (maxTextLines < 1)
        maxTextLines = 1;

    const bool everythingFits = (totalLines <= maxTextLines);
    int totalPages = 1;
    if (!everythingFits)
    {
        maxTextLines = std::max(1, maxTextLines);
        const int remainingLines = totalLines - maxTextLines;
        totalPages = 1 + (remainingLines + maxTextLines - 1) / maxTextLines;
    }
    m_DialogueUi.totalPages = totalPages;
    if (m_DialogueUi.page >= totalPages)
        m_DialogueUi.page = totalPages - 1;
    if (m_DialogueUi.page < 0)
        m_DialogueUi.page = 0;
    const bool isLastPage = (m_DialogueUi.page == totalPages - 1);

    int startLine = 0;
    int linesToShow = 0;
    if (totalPages == 1)
    {
        startLine = 0;
        linesToShow = totalLines;
    }
    else if (isLastPage)
    {
        startLine = m_DialogueUi.page * maxTextLines;
        linesToShow = totalLines - startLine;
    }
    else
    {
        startLine = m_DialogueUi.page * maxTextLines;
        linesToShow = maxTextLines;
    }

    // Typewriter reveal.
    int totalCharsOnPage = 0;
    for (int i = 0; i < linesToShow && (startLine + i) < totalLines; ++i)
        totalCharsOnPage += static_cast<int>(allLines[startLine + i].size());
    const int charsToShow = (m_DialogueUi.charReveal < 0.0f)
                                ? totalCharsOnPage
                                : static_cast<int>(m_DialogueUi.charReveal);
    const bool textFullyRevealed = (charsToShow >= totalCharsOnPage);

    // Body text on slate (cream fill + black outline for legibility).
    float currentY = bodyTextStartY;
    int charsRemaining = charsToShow;
    const glm::vec3 bodyTextColor = ambience::DIALOGUE_BODY_TEXT_COLOR;
    for (int i = 0; i < linesToShow && (startLine + i) < totalLines; ++i)
    {
        const std::string& line = allLines[startLine + i];
        const int lineLen = static_cast<int>(line.size());
        if (charsRemaining <= 0)
            break;
        if (charsRemaining >= lineLen)
        {
            m_Renderer->DrawText(line,
                                 glm::vec2(textAreaLeft, currentY),
                                 textScale,
                                 bodyTextColor,
                                 bodyOutlineSize,
                                 textAlpha);
        }
        else
        {
            if (charsRemaining > 0)
            {
                m_Renderer->DrawText(line.substr(0, charsRemaining),
                                     glm::vec2(textAreaLeft, currentY),
                                     textScale,
                                     bodyTextColor,
                                     bodyOutlineSize,
                                     textAlpha);
            }
            if (m_DialogueUi.charReveal >= 0.0f && charsRemaining < lineLen)
            {
                const float partialFrac =
                    m_DialogueUi.charReveal - std::floor(m_DialogueUi.charReveal);
                if (partialFrac > 0.0f)
                {
                    const float xOffset =
                        (charsRemaining > 0)
                            ? m_Renderer->GetTextWidth(line.substr(0, charsRemaining), textScale)
                            : 0.0f;
                    m_Renderer->DrawText(line.substr(charsRemaining, 1),
                                         glm::vec2(textAreaLeft + xOffset, currentY),
                                         textScale,
                                         bodyTextColor,
                                         bodyOutlineSize,
                                         textAlpha * partialFrac);
                }
            }
        }
        charsRemaining -= lineLen;
        currentY += lineHeight;
    }
    currentY += 1.0f * z;

    // Continue prompt or response options.
    const float promptY = boxY + boxHeight - padInner - padding * 0.4f;
    const float promptX = boxX + boxWidth - padInner - padding - 16.0f * z;
    const bool showContinuePrompt = !isLastPage || visibleOptions.empty();
    if (!textFullyRevealed)
    {
        // Wait for typewriter to complete before showing prompt/options.
    }
    else if (showContinuePrompt)
    {
        DrawContinuePrompt(*m_Renderer,
                           promptX,
                           promptY,
                           textScale,
                           ribbonOutlineSize,
                           z,
                           fadeAlpha,
                           m_DialogueUi.boxFadeTimer);
    }
    else
    {
        // Last page with options: accent triangle on the selected one.
        const int selectedIndex = m_DialogueManager.GetSelectedOptionIndex();
        for (size_t i = 0; i < visibleOptions.size(); ++i)
        {
            const DialogueOption* opt = visibleOptions[i];
            const bool isSelected = (static_cast<int>(i) == selectedIndex);

            if (isSelected)
            {
                // Sine pulse on the accent triangle.
                const float pulseAlpha =
                    ambience::DIALOGUE_ARROW_PULSE_BASE +
                    ambience::DIALOGUE_ARROW_PULSE_AMPLITUDE *
                        std::sin(m_DialogueUi.boxFadeTimer * ambience::DIALOGUE_ARROW_PULSE_HZ *
                                 6.28318530f);
                const float triH = ambience::DIALOGUE_SELECTION_TRIANGLE_H * z;
                const float triCenterY = currentY - textAscent * 0.5f;
                const float triX = textAreaLeft;
                const float triY = triCenterY - triH * 0.5f;
                DrawAccentTriangle(*m_Renderer,
                                   glm::vec2(SnapToPixel(triX, z), SnapToPixel(triY, z)),
                                   accent,
                                   z,
                                   fadeAlpha * pulseAlpha);
            }

            const std::string prefix = "   ";  // Indent so option text clears the triangle.
            // Selected option uses full cream; others dim to ~70% so the eye
            // still picks out the active line.
            const glm::vec3 optionColor = isSelected ? ambience::DIALOGUE_BODY_TEXT_COLOR
                                                     : ambience::DIALOGUE_BODY_TEXT_COLOR * 0.70f;

            // Quest detection: "accepted_*_quest" flag => quest option.
            bool givesQuest = false;
            for (const auto& cons : opt->consequences)
            {
                if ((cons.type == DialogueConsequence::Type::SET_FLAG ||
                     cons.type == DialogueConsequence::Type::SET_FLAG_VALUE) &&
                    cons.key.find("accepted_") == 0 && cons.key.size() >= 6 &&
                    cons.key.compare(cons.key.size() - 6, 6, "_quest") == 0)
                {
                    givesQuest = true;
                    break;
                }
            }

            std::string displayText = prefix + opt->text;
            // Binary-search ellipsis fit if the option text overflows.
            const float optMaxWidth = textAreaRight - textAreaLeft;
            if (m_Renderer->GetTextWidth(displayText, textScale) > optMaxWidth)
            {
                const std::string ellipsis = "...";
                const float ellipsisWidth = m_Renderer->GetTextWidth(ellipsis, textScale);
                size_t lo = 1;
                size_t hi = displayText.size();
                while (lo < hi)
                {
                    const size_t mid = lo + (hi - lo + 1) / 2;
                    if (m_Renderer->GetTextWidth(displayText.substr(0, mid), textScale) +
                            ellipsisWidth <=
                        optMaxWidth)
                    {
                        lo = mid;
                    }
                    else
                    {
                        hi = mid - 1;
                    }
                }
                displayText = displayText.substr(0, lo) + ellipsis;
            }
            m_Renderer->DrawText(displayText,
                                 glm::vec2(textAreaLeft, currentY),
                                 textScale,
                                 optionColor,
                                 bodyOutlineSize,
                                 textAlpha);

            // Gold quest marker.
            if (givesQuest)
            {
                const glm::vec3 questYellow(0.85f, 0.65f, 0.20f);
                const float w = m_Renderer->GetTextWidth(prefix + opt->text + " ", textScale);
                m_Renderer->DrawText(">!<",
                                     glm::vec2(textAreaLeft + w, currentY),
                                     textScale,
                                     questYellow,
                                     bodyOutlineSize,
                                     textAlpha);
            }
            currentY += lineHeight;
        }
    }
}

bool Game::IsDialogueOnLastPage()
{
    return m_DialogueUi.page >= m_DialogueUi.totalPages - 1;
}
