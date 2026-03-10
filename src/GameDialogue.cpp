#include "DialogueManager.h"
#include "DialogueSystem.h"
#include "Game.h"

#include <functional>
#include <iostream>
#include <vector>

namespace
{
/**
 * Word-wrap text to fit within a maximum width.
 *
 * NOTE: This is ASCII/space-delimited only; UTF-8 glyphs and very long
 * tokens are not split. The renderer must be able to measure each whole
 * line via measureWidth().
 *
 * @param text         The text to wrap.
 * @param maxWidth     Maximum line width in pixels.
 * @param measureWidth Function to measure the pixel width of a string.
 * @return Vector of wrapped lines.
 */
std::vector<std::string> WrapText(const std::string& text,
                                  float maxWidth,
                                  const std::function<float(const std::string&)>& measureWidth)
{
    std::vector<std::string> lines;
    std::string currentLine;
    std::string word;

    // Add current word to line, wrapping if it exceeds max width
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

    // Finish current line and start a new one
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

    return lines;
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

void DrawContinuePrompt(
    IRenderer& renderer, float promptX, float promptY, float textScale, float outlineSize, float z)
{
    const float promptScale = textScale * 0.85f;
    glm::vec3 promptColor(0.55f, 0.52f, 0.48f);
    renderer.DrawText(
        "Continue", glm::vec2(promptX, promptY), promptScale, promptColor, outlineSize, 0.7f);

    float promptAscent = renderer.GetTextAscent(promptScale);
    float arrowCenterY = promptY - promptAscent * 0.5f;
    float arrowX = promptX - 6.0f * z;
    DrawRightArrow(renderer, arrowX, arrowCenterY, z, glm::vec4(0.65f, 0.52f, 0.2f, 0.85f));
}
}  // namespace

void Game::RenderNPCHeadText()
{
    if (!m_InDialogue || m_DialogueText.empty() || m_DialogueNPCIndex < 0 ||
        m_DialogueNPCIndex >= static_cast<int>(m_NPCs.size()))
    {
        return;
    }

    // Get NPC position in screen space
    glm::vec2 npcWorldPos = m_NPCs[m_DialogueNPCIndex].GetPosition();
    glm::vec2 npcScreenPos = npcWorldPos - m_CameraPosition;

    // Position text above the NPC's head
    float textAreaWidth = 180.0f;
    const float NPC_SPRITE_HEIGHT = PlayerCharacter::RENDER_HEIGHT;
    float npcTopY = npcScreenPos.y - NPC_SPRITE_HEIGHT;
    float npcCenterX = npcScreenPos.x;

    glm::vec2 textAreaPos(npcCenterX - textAreaWidth * 0.5f, npcTopY - 10.0f);
    // TODO: Fixed height for now, should adjust based on zoom level
    // TODO: Clamp to the visible screen so head text cannot render off-screen.
    glm::vec2 textAreaSize(textAreaWidth, 50.0f);

    RenderDialogueText(textAreaPos, textAreaSize);
}

void Game::RenderDialogueText(glm::vec2 boxPos, glm::vec2 boxSize)
{
    if (m_DialogueText.empty())
    {
        return;
    }

    float scale = 0.2f;
    float lineHeight = 6.0f;
    float maxWidth = boxSize.x - 20.0f;

    auto lines = WrapText(m_DialogueText,
                          maxWidth,
                          [&](const std::string& s) { return m_Renderer->GetTextWidth(s, scale); });

    // Render each line, centered horizontally
    float currentY = boxPos.y;
    glm::vec3 textColor(1.0f, 1.0f, 1.0f);

    for (const std::string& line : lines)
    {
        if (!line.empty())
        {
            float lineWidth = m_Renderer->GetTextWidth(line, scale);
            float lineStartX = boxPos.x + (boxSize.x - lineWidth) * 0.5f;
            m_Renderer->DrawText(line, glm::vec2(lineStartX, currentY), scale, textColor);
        }
        currentY += lineHeight;
    }
    // TODO: Clip or truncate lines when they exceed box height; currently they can spill past the
    // box.
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

    // Get world dimensions for positioning, adjusted for zoom
    float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float baseWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    float worldWidth = baseWorldWidth / m_CameraZoom;
    float worldHeight = baseWorldHeight / m_CameraZoom;

    // Scale factor for UI elements, inverse of zoom so they appear constant size on screen
    float z = 1.0f / m_CameraZoom;

    // Dialogue box dimensions and position (fixed at bottom of visible screen)
    float boxWidth = baseWorldWidth * 0.9f * z;
    float boxHeight = 60.0f * z;
    float boxX = (worldWidth - boxWidth) * 0.5f;
    float boxY = worldHeight - boxHeight - (10.0f * z);

    // Rounded corners are drawn as a "staircase" of progressively wider
    // horizontal strips. Row 0 is the narrowest (inset by full radius),
    // each subsequent row is 1px wider, until the body is reached at full
    // width. This avoids needing a shader or anti-aliased geometry.
    glm::vec4 bgColor(0.22f, 0.21f, 0.20f, 0.92f);
    float r = 3.0f * z;  // Corner radius
    float s = 1.0f * z;  // Step size (1 pixel per step)
    // Row 0 (top/bottom outermost): inset by r on each side
    m_Renderer->DrawColoredRect(glm::vec2(boxX + r, boxY), glm::vec2(boxWidth - r * 2, s), bgColor);
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + r, boxY + boxHeight - s), glm::vec2(boxWidth - r * 2, s), bgColor);
    // Row 1: inset by 2px
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + 2 * s, boxY + s), glm::vec2(boxWidth - 4 * s, s), bgColor);
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + 2 * s, boxY + boxHeight - 2 * s), glm::vec2(boxWidth - 4 * s, s), bgColor);
    // Row 2: inset by 1px
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + s, boxY + 2 * s), glm::vec2(boxWidth - 2 * s, s), bgColor);
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + s, boxY + boxHeight - 3 * s), glm::vec2(boxWidth - 2 * s, s), bgColor);
    // Center body: full width
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX, boxY + r), glm::vec2(boxWidth, boxHeight - r * 2), bgColor);

    // Outer border - muted, subtle, following the rounded shape
    float bw = 1.0f * z;
    glm::vec4 borderColorOuter(0.50f, 0.48f, 0.45f, 0.8f);
    // Top edge (inset by r)
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + r, boxY), glm::vec2(boxWidth - r * 2, bw), borderColorOuter);
    // Bottom edge (inset by r)
    m_Renderer->DrawColoredRect(glm::vec2(boxX + r, boxY + boxHeight - bw),
                                glm::vec2(boxWidth - r * 2, bw),
                                borderColorOuter);
    // Left edge (inset by r vertically)
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX, boxY + r), glm::vec2(bw, boxHeight - r * 2), borderColorOuter);
    // Right edge (inset by r vertically)
    m_Renderer->DrawColoredRect(glm::vec2(boxX + boxWidth - bw, boxY + r),
                                glm::vec2(bw, boxHeight - r * 2),
                                borderColorOuter);
    // Corner steps: top-left
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + s, boxY + 2 * s), glm::vec2(bw, s), borderColorOuter);
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + 2 * s, boxY + s), glm::vec2(bw, s), borderColorOuter);
    // Corner steps: top-right
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + boxWidth - s - bw, boxY + 2 * s), glm::vec2(bw, s), borderColorOuter);
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + boxWidth - 2 * s - bw, boxY + s), glm::vec2(bw, s), borderColorOuter);
    // Corner steps: bottom-left
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + s, boxY + boxHeight - 3 * s), glm::vec2(bw, s), borderColorOuter);
    m_Renderer->DrawColoredRect(
        glm::vec2(boxX + 2 * s, boxY + boxHeight - 2 * s), glm::vec2(bw, s), borderColorOuter);
    // Corner steps: bottom-right
    m_Renderer->DrawColoredRect(glm::vec2(boxX + boxWidth - s - bw, boxY + boxHeight - 3 * s),
                                glm::vec2(bw, s),
                                borderColorOuter);
    m_Renderer->DrawColoredRect(glm::vec2(boxX + boxWidth - 2 * s - bw, boxY + boxHeight - 2 * s),
                                glm::vec2(bw, s),
                                borderColorOuter);

    // Inner border - subtle accent, following the rounded shape
    float ibo = 3.0f * z;  // inner border offset
    float ibw = 1.0f * z;
    glm::vec4 borderColorInner(0.42f, 0.40f, 0.37f, 0.5f);
    m_Renderer->DrawColoredRect(glm::vec2(boxX + ibo + r, boxY + ibo),
                                glm::vec2(boxWidth - ibo * 2 - r * 2, ibw),
                                borderColorInner);  // Top
    m_Renderer->DrawColoredRect(glm::vec2(boxX + ibo + r, boxY + boxHeight - ibo - ibw),
                                glm::vec2(boxWidth - ibo * 2 - r * 2, ibw),
                                borderColorInner);  // Bottom
    m_Renderer->DrawColoredRect(glm::vec2(boxX + ibo, boxY + ibo + r),
                                glm::vec2(ibw, boxHeight - ibo * 2 - r * 2),
                                borderColorInner);  // Left
    m_Renderer->DrawColoredRect(glm::vec2(boxX + boxWidth - ibo - ibw, boxY + ibo + r),
                                glm::vec2(ibw, boxHeight - ibo * 2 - r * 2),
                                borderColorInner);  // Right

    float padding = 10.0f * z;
    float textScale = 0.18f * z;
    float lineHeight = 5.5f * z;
    float contentTopMargin = 4.0f * z;  // Extra space at top for nameplate
    float contentStartY = boxY + padding + contentTopMargin;
    float currentY = contentStartY;

    // Get text ascent for proper alignment
    float textAscent = m_Renderer->GetTextAscent(textScale);
    float outlineSize = 2.0f;  // Constant outline size
    // TODO: Scale outlineSize by z to keep stroke weight visually consistent across zoom levels.
    float textAlpha = 1.0f;  // Full opacity text

    // Calculate available content height
    float contentBottomY = boxY + boxHeight - padding;
    float availableHeight = contentBottomY - contentStartY;

    float speakerHeight = 0.0f;
    if (!node->speaker.empty())
    {
        // Speaker nameplate background
        float speakerScale = textScale * 1.2f;
        float speakerAscent = m_Renderer->GetTextAscent(speakerScale);
        float namePadding = 4.0f * z;  // Padding on left and right inside nameplate
        float actualNameWidth = m_Renderer->GetTextWidth(node->speaker, speakerScale);
        float nameWidth = actualNameWidth + namePadding * 2;
        float nameHeight = speakerAscent + 4.0f * z;
        float nameX = boxX + padding - namePadding;
        float nameY = currentY - speakerAscent - 2.0f * z;

        // Nameplate background - darker muted gold, with rounded corners
        glm::vec4 nameBg(0.38f, 0.36f, 0.30f, 0.9f);
        float nr = 2.0f * z;  // Nameplate corner radius
        float ns = 1.0f * z;
        // Top/bottom outermost row: inset by nr
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX + nr, nameY), glm::vec2(nameWidth - nr * 2, ns), nameBg);
        m_Renderer->DrawColoredRect(glm::vec2(nameX + nr, nameY + nameHeight - ns),
                                    glm::vec2(nameWidth - nr * 2, ns),
                                    nameBg);
        // Intermediate row: inset by 1px
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX + ns, nameY + ns), glm::vec2(nameWidth - 2 * ns, ns), nameBg);
        m_Renderer->DrawColoredRect(glm::vec2(nameX + ns, nameY + nameHeight - 2 * ns),
                                    glm::vec2(nameWidth - 2 * ns, ns),
                                    nameBg);
        // Center body: full width
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX, nameY + nr), glm::vec2(nameWidth, nameHeight - nr * 2), nameBg);

        // Nameplate border - subtle, following rounded shape
        glm::vec4 nameBorder(0.50f, 0.48f, 0.44f, 0.5f);
        float nb = 1.0f * z;
        // Top/bottom edges inset by nr
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX + nr, nameY), glm::vec2(nameWidth - nr * 2, nb), nameBorder);
        m_Renderer->DrawColoredRect(glm::vec2(nameX + nr, nameY + nameHeight - nb),
                                    glm::vec2(nameWidth - nr * 2, nb),
                                    nameBorder);
        // Left/right edges inset by nr
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX, nameY + nr), glm::vec2(nb, nameHeight - nr * 2), nameBorder);
        m_Renderer->DrawColoredRect(glm::vec2(nameX + nameWidth - nb, nameY + nr),
                                    glm::vec2(nb, nameHeight - nr * 2),
                                    nameBorder);
        // Corner steps
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX + ns, nameY + ns), glm::vec2(nb, ns), nameBorder);  // TL
        m_Renderer->DrawColoredRect(glm::vec2(nameX + nameWidth - ns - nb, nameY + ns),
                                    glm::vec2(nb, ns),
                                    nameBorder);  // TR
        m_Renderer->DrawColoredRect(glm::vec2(nameX + ns, nameY + nameHeight - 2 * ns),
                                    glm::vec2(nb, ns),
                                    nameBorder);  // BL
        m_Renderer->DrawColoredRect(
            glm::vec2(nameX + nameWidth - ns - nb, nameY + nameHeight - 2 * ns),
            glm::vec2(nb, ns),
            nameBorder);  // BR

        glm::vec3 speakerColor(0.85f, 0.75f, 0.40f);
        m_Renderer->DrawText(node->speaker,
                             glm::vec2(boxX + padding, currentY - 1.0f * z),
                             speakerScale,
                             speakerColor,
                             outlineSize,
                             textAlpha);
        speakerHeight = lineHeight + 4.0f * z;
        currentY += speakerHeight;
    }

    const float maxTextWidth = boxWidth - padding * 2;
    auto allLines =
        WrapText(node->text,
                 maxTextWidth,
                 [&](const std::string& s) { return m_Renderer->GetTextWidth(s, textScale); });

    const auto& visibleOptions = m_DialogueManager.GetVisibleOptions();
    int numOptions = static_cast<int>(visibleOptions.size());

    // Text pagination: the dialogue box has a fixed pixel height. We need to
    // fit the speaker name, NPC text, and response options all inside it.
    // Options are anchored to the bottom, so text gets whatever space remains.
    // If the text doesn't fit, we split it into pages the player can advance.
    float heightAfterSpeaker = availableHeight - speakerHeight;
    int totalLines = static_cast<int>(allLines.size());

    // Subtract the space reserved for response options at the bottom.
    // The (padding - optionsBottomPadding) term reclaims unused padding between
    // the last text line and the first option.
    float optionsBottomPadding = 7.0f * z;
    float effectiveOptionsSpace =
        static_cast<float>(numOptions) * lineHeight - (padding - optionsBottomPadding);
    if (effectiveOptionsSpace < 0)
        effectiveOptionsSpace = 0;
    float spaceForText = heightAfterSpeaker - effectiveOptionsSpace;
    // +1 because int truncation loses a partial line that still fits
    int maxTextLines = static_cast<int>(spaceForText / lineHeight) + 1;
    if (maxTextLines < 1)
        maxTextLines = 1;

    bool everythingFits = (totalLines <= maxTextLines);
    int totalPages = 1;

    if (!everythingFits)
    {
        // Ceiling division: how many pages of maxTextLines to show all text
        int remainingLines = totalLines - maxTextLines;
        totalPages = 1 + (remainingLines + maxTextLines - 1) / maxTextLines;
    }
    m_DialogueTotalPages = totalPages;

    // Clamp current page
    if (m_DialoguePage >= totalPages)
        m_DialoguePage = totalPages - 1;
    if (m_DialoguePage < 0)
        m_DialoguePage = 0;

    bool isLastPage = (m_DialoguePage == totalPages - 1);

    // Calculate which lines to show on current page
    int startLine = 0;
    int linesToShow = 0;

    if (totalPages == 1)
    {
        // Everything fits
        startLine = 0;
        linesToShow = totalLines;
    }
    else if (isLastPage)
    {
        // Last page shows remaining lines that fit above options
        startLine = m_DialoguePage * maxTextLines;
        linesToShow = totalLines - startLine;
    }
    else
    {
        // Earlier pages show maxTextLines worth of text
        startLine = m_DialoguePage * maxTextLines;
        linesToShow = maxTextLines;
    }

    // Render dialogue text lines
    glm::vec3 textColor(0.82f, 0.80f, 0.75f);
    for (int i = 0; i < linesToShow && (startLine + i) < totalLines; ++i)
    {
        m_Renderer->DrawText(allLines[startLine + i],
                             glm::vec2(boxX + padding, currentY),
                             textScale,
                             textColor,
                             outlineSize,
                             textAlpha);
        currentY += lineHeight;
    }
    currentY += 1.0f * z;

    // Position for bottom-right prompt
    float promptY = boxY + boxHeight - padding;
    float promptX = boxX + boxWidth - padding - 12.0f * z;

    const bool showContinuePrompt = !isLastPage || visibleOptions.empty();
    if (showContinuePrompt)
    {
        DrawContinuePrompt(*m_Renderer, promptX, promptY, textScale, outlineSize, z);
    }
    else
    {
        // Last page with options - show response options right under the text
        int selectedIndex = m_DialogueManager.GetSelectedOptionIndex();

        for (size_t i = 0; i < visibleOptions.size(); ++i)
        {
            const DialogueOption* opt = visibleOptions[i];
            bool isSelected = (static_cast<int>(i) == selectedIndex);

            if (isSelected)
            {
                float arrowCenterY = currentY - textAscent * 0.5f;
                float arrowX = boxX + padding;
                DrawRightArrow(
                    *m_Renderer, arrowX, arrowCenterY, z, glm::vec4(1.0f, 0.88f, 0.4f, 1.0f));
            }

            std::string prefix = "   ";
            glm::vec3 optionColor =
                isSelected ? glm::vec3(0.85f, 0.75f, 0.40f) : glm::vec3(0.58f, 0.55f, 0.50f);

            // Detect quest-giving options by convention: any consequence that
            // sets a flag matching "accepted_*_quest" is treated as a quest offer.
            // This lets designers mark quest options purely through flag naming.
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
            // TODO: Wrap long option text to maxTextWidth; current rendering can overflow the box.
            m_Renderer->DrawText(displayText,
                                 glm::vec2(boxX + padding, currentY),
                                 textScale,
                                 optionColor,
                                 outlineSize,
                                 textAlpha);

            // Draw the exclamation mark in gold if this is a quest option
            if (givesQuest)
            {
                glm::vec3 questYellow(1.0f, 0.88f, 0.4f);
                float textWidth = m_Renderer->GetTextWidth(prefix + opt->text + " ", textScale);
                float exclamationX = boxX + padding + textWidth;
                m_Renderer->DrawText(">!<",
                                     glm::vec2(exclamationX, currentY),
                                     textScale,
                                     questYellow,
                                     outlineSize,
                                     1.0f);
            }
            currentY += lineHeight;
        }
    }
}

bool Game::IsDialogueOnLastPage() const
{
    return m_DialoguePage >= m_DialogueTotalPages - 1;
}
