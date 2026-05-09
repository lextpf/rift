#include "Console.h"

#include "IRenderer.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

// ============================================================================
// ConsoleBuffer
// ============================================================================

void ConsoleBuffer::Print(std::string text, glm::vec3 color)
{
    m_Lines.push_back({std::move(text), color});
    while (m_Lines.size() > MAX_LINES)
    {
        m_Lines.pop_front();
    }
    // Auto-snap to bottom whenever new output arrives.
    m_ScrollOffset = 0;
}

void ConsoleBuffer::PrintError(std::string text)
{
    Print(std::move(text), glm::vec3(1.0f, 0.45f, 0.45f));
}

void ConsoleBuffer::Clear()
{
    m_Lines.clear();
    m_ScrollOffset = 0;
}

void ConsoleBuffer::OnChar(std::uint32_t codepoint)
{
    // ASCII printable range only for the MVP. Non-ASCII typing (accented
    // glyphs, IME composition) is out of scope for a developer console.
    if (codepoint < 0x20 || codepoint > 0x7E)
    {
        return;
    }
    m_Input.insert(m_CursorPos, 1, static_cast<char>(codepoint));
    ++m_CursorPos;
    ResetHistoryIndex();
}

void ConsoleBuffer::OnBackspace()
{
    if (m_CursorPos == 0)
    {
        return;
    }
    m_Input.erase(m_CursorPos - 1, 1);
    --m_CursorPos;
    ResetHistoryIndex();
}

void ConsoleBuffer::OnBackspaceWord()
{
    if (m_CursorPos == 0)
    {
        return;
    }
    // Word boundaries are spaces (between command and arguments) and dots
    // (between dot-segmented command parts like `npc.freeze`). Treating both
    // as stop characters lets Ctrl+Backspace walk back segment-by-segment:
    // "npc.freeze" -> "npc." -> "" and
    // "time.weather clear" -> "time.weather " -> "time." -> "".
    auto isBoundary = [](char c) { return c == ' ' || c == '.'; };

    // First eat any boundary chars immediately before the cursor. This
    // handles the case where the cursor sits just after a word boundary -
    // e.g. after a prior Ctrl+Backspace left "time.weather " with the cursor
    // at the end: the next press should remove the trailing space and the
    // preceding word in one action.
    while (m_CursorPos > 0 && isBoundary(m_Input[m_CursorPos - 1]))
    {
        m_Input.erase(m_CursorPos - 1, 1);
        --m_CursorPos;
    }
    // Then eat one contiguous run of non-boundary characters (the word).
    while (m_CursorPos > 0 && !isBoundary(m_Input[m_CursorPos - 1]))
    {
        m_Input.erase(m_CursorPos - 1, 1);
        --m_CursorPos;
    }
    ResetHistoryIndex();
}

void ConsoleBuffer::OnDelete()
{
    if (m_CursorPos >= m_Input.size())
    {
        return;
    }
    m_Input.erase(m_CursorPos, 1);
    ResetHistoryIndex();
}

void ConsoleBuffer::OnLeft()
{
    if (m_CursorPos > 0)
    {
        --m_CursorPos;
    }
}

void ConsoleBuffer::OnRight()
{
    if (m_CursorPos < m_Input.size())
    {
        ++m_CursorPos;
    }
}

void ConsoleBuffer::OnHome()
{
    m_CursorPos = 0;
}

void ConsoleBuffer::OnEnd()
{
    m_CursorPos = m_Input.size();
}

std::string ConsoleBuffer::OnEnter()
{
    std::string submitted = std::move(m_Input);
    m_Input.clear();
    m_CursorPos = 0;
    ResetHistoryIndex();
    return submitted;
}

void ConsoleBuffer::SetInputLine(std::string text)
{
    m_Input = std::move(text);
    m_CursorPos = m_Input.size();
}

void ConsoleBuffer::RecordHistory(std::string command)
{
    if (command.empty())
    {
        return;
    }
    if (!m_History.empty() && m_History.back() == command)
    {
        return;
    }
    m_History.push_back(std::move(command));
    while (m_History.size() > MAX_HISTORY)
    {
        m_History.erase(m_History.begin());
    }
}

std::optional<std::string> ConsoleBuffer::HistoryPrev()
{
    if (m_History.empty())
    {
        return std::nullopt;
    }
    if (!m_HistoryIdx.has_value())
    {
        m_HistoryIdx = m_History.size() - 1;
        return m_History[*m_HistoryIdx];
    }
    if (*m_HistoryIdx == 0)
    {
        return std::nullopt;  // already at oldest
    }
    --*m_HistoryIdx;
    return m_History[*m_HistoryIdx];
}

std::optional<std::string> ConsoleBuffer::HistoryNext()
{
    if (!m_HistoryIdx.has_value())
    {
        return std::nullopt;
    }
    if (*m_HistoryIdx + 1 >= m_History.size())
    {
        // Stepping past the newest entry returns to a fresh empty prompt.
        m_HistoryIdx.reset();
        return std::string{};
    }
    ++*m_HistoryIdx;
    return m_History[*m_HistoryIdx];
}

void ConsoleBuffer::ResetHistoryIndex()
{
    m_HistoryIdx.reset();
}

void ConsoleBuffer::Scroll(int deltaLines)
{
    int next = m_ScrollOffset + deltaLines;
    const int maxOffset = static_cast<int>(m_Lines.size());
    if (next < 0)
    {
        next = 0;
    }
    if (next > maxOffset)
    {
        next = maxOffset;
    }
    m_ScrollOffset = next;
}

void ConsoleBuffer::ResetScroll()
{
    m_ScrollOffset = 0;
}

// ============================================================================
// ConsoleCommandRegistry
// ============================================================================

void ConsoleCommandRegistry::Register(std::string name,
                                      std::string description,
                                      Handler handler,
                                      std::vector<std::string> aliases,
                                      ArgCompletionProvider argCompletions)
{
    if (name.empty())
    {
        return;
    }
    Command cmd{name,
                std::move(description),
                std::move(handler),
                std::move(aliases),
                std::move(argCompletions)};
    m_Commands[std::move(name)] = std::move(cmd);
}

const ConsoleCommandRegistry::Command* ConsoleCommandRegistry::Lookup(std::string_view name) const
{
    auto it = m_Commands.find(std::string(name));
    if (it != m_Commands.end())
    {
        return &it->second;
    }
    for (const auto& [_canonical, cmd] : m_Commands)
    {
        for (const auto& alias : cmd.aliases)
        {
            if (alias == name)
            {
                return &cmd;
            }
        }
    }
    return nullptr;
}

std::vector<std::string> ConsoleCommandRegistry::MatchPrefix(std::string_view prefix,
                                                             std::size_t maxCount) const
{
    auto startsWith = [&](std::string_view s)
    { return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix; };

    std::vector<std::string> out;
    for (const auto& [name, cmd] : m_Commands)
    {
        if (startsWith(name))
        {
            out.push_back(name);
        }
        for (const auto& alias : cmd.aliases)
        {
            if (startsWith(alias))
            {
                out.push_back(alias);
            }
        }
    }
    std::sort(out.begin(), out.end());
    if (out.size() > maxCount)
    {
        out.resize(maxCount);
    }
    return out;
}

// ============================================================================
// Console - input wiring, parsing, dispatch
// ============================================================================

std::vector<std::string_view> Console::Tokenize(std::string_view line)
{
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        {
            ++i;
        }
        if (i >= line.size())
        {
            break;
        }
        std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
        {
            ++i;
        }
        tokens.emplace_back(line.substr(start, i - start));
    }
    return tokens;
}

Console::SuggestionResult Console::ComputeSuggestions(std::size_t maxCount) const
{
    SuggestionResult result;
    const std::string& input = m_Buffer.Input();
    if (input.empty() || maxCount == 0)
    {
        return result;
    }

    // The "current word" is everything after the last space in the input.
    // For first-token completion this is the whole input; for parameter
    // completion it is the partial argument the user is currently typing.
    const std::size_t lastSpace = input.find_last_of(' ');
    result.wordStart = (lastSpace == std::string::npos) ? 0u : lastSpace + 1;
    const std::string_view currentWord = std::string_view(input).substr(result.wordStart);

    if (result.wordStart == 0)
    {
        // No spaces yet - completing the verb itself.
        result.items = m_Registry.MatchPrefix(currentWord, maxCount);
        return result;
    }

    // Argument completion. Tokenize the prefix to find the verb and which
    // positional argument index the user is on.
    const std::string_view beforeWord = std::string_view(input).substr(0, result.wordStart);
    auto tokens = Tokenize(beforeWord);
    if (tokens.empty())
    {
        return result;
    }
    const auto* cmd = m_Registry.Lookup(tokens.front());
    if (cmd == nullptr || !cmd->argCompletions)
    {
        return result;
    }
    // tokens[0] is the verb; positional args follow. We're typing arg index
    // `tokens.size() - 1` (zero for the first arg after the verb).
    const std::size_t argIndex = tokens.size() - 1;
    auto candidates = cmd->argCompletions(argIndex);

    auto startsWith = [&](std::string_view s)
    { return s.size() >= currentWord.size() && s.substr(0, currentWord.size()) == currentWord; };
    for (auto& c : candidates)
    {
        if (startsWith(c))
        {
            result.items.push_back(std::move(c));
        }
    }
    std::sort(result.items.begin(), result.items.end());
    if (result.items.size() > maxCount)
    {
        result.items.resize(maxCount);
    }
    return result;
}

Console::Console(Game& game)
    : m_Game(game)
{
    RegisterDefaultCommands();
}

void Console::Toggle()
{
    m_State = NextConsoleState(m_State);
    if (m_State == State::Half)
    {
        // Just transitioned from Closed -> Half; pin scroll so a new opener
        // sees the most recent output, not stale offset from a prior session.
        m_Buffer.ResetScroll();
    }
}

void Console::Open()
{
    m_State = State::Half;
    m_Buffer.ResetScroll();
}

void Console::Close()
{
    m_State = State::Closed;
}

void Console::ClampSuggestionScroll(std::size_t itemCount)
{
    if (itemCount == 0)
    {
        m_SuggestionScroll = 0;
        return;
    }
    if (m_SuggestionIndex < m_SuggestionScroll)
    {
        m_SuggestionScroll = m_SuggestionIndex;
    }
    if (m_SuggestionIndex >= m_SuggestionScroll + kMaxVisibleSuggestions)
    {
        m_SuggestionScroll = m_SuggestionIndex - kMaxVisibleSuggestions + 1;
    }
    const std::size_t maxScroll =
        (itemCount > kMaxVisibleSuggestions) ? itemCount - kMaxVisibleSuggestions : 0;
    if (m_SuggestionScroll > maxScroll)
    {
        m_SuggestionScroll = maxScroll;
    }
}

void Console::OnChar(std::uint32_t codepoint)
{
    if (m_State == State::Closed)
    {
        return;
    }
    m_Buffer.OnChar(codepoint);
    m_SuggestionIndex = 0;
    m_SuggestionScroll = 0;
}

void Console::OnEnter()
{
    if (m_State == State::Closed)
    {
        return;
    }
    std::string line = m_Buffer.OnEnter();
    m_SuggestionIndex = 0;
    m_SuggestionScroll = 0;
    if (line.empty())
    {
        return;
    }
    m_Buffer.RecordHistory(line);
    Submit(line);
}

void Console::OnBackspace()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnBackspace();
    m_SuggestionIndex = 0;
    m_SuggestionScroll = 0;
}

void Console::OnBackspaceWord()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnBackspaceWord();
    m_SuggestionIndex = 0;
    m_SuggestionScroll = 0;
}

void Console::OnDelete()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnDelete();
    m_SuggestionIndex = 0;
    m_SuggestionScroll = 0;
}

void Console::OnTab()
{
    if (m_State == State::Closed)
    {
        return;
    }
    auto sugg = ComputeSuggestions(kMaxSuggestions);
    if (sugg.items.empty())
    {
        return;
    }
    const std::size_t idx = std::min(m_SuggestionIndex, sugg.items.size() - 1);
    std::string newInput = m_Buffer.Input().substr(0, sugg.wordStart) + sugg.items[idx];
    if (newInput != m_Buffer.Input())
    {
        m_Buffer.SetInputLine(std::move(newInput));
    }
    // After a fill the suggestion list usually shrinks; reset selection so the
    // next dropdown opens at the top.
    m_SuggestionIndex = 0;
    m_SuggestionScroll = 0;
}

void Console::OnUp()
{
    if (m_State == State::Closed)
        return;
    // When the suggestion dropdown is visible, arrow keys navigate it and
    // history navigation is suppressed - the user is choosing a completion,
    // not recalling a prior command.
    auto sugg = ComputeSuggestions(kMaxSuggestions);
    if (!sugg.items.empty())
    {
        if (m_SuggestionIndex > 0)
        {
            --m_SuggestionIndex;
        }
        ClampSuggestionScroll(sugg.items.size());
        return;
    }
    auto recalled = m_Buffer.HistoryPrev();
    if (recalled.has_value())
    {
        m_Buffer.SetInputLine(std::move(*recalled));
        m_SuggestionIndex = 0;
        m_SuggestionScroll = 0;
    }
}

void Console::OnDown()
{
    if (m_State == State::Closed)
        return;
    auto sugg = ComputeSuggestions(kMaxSuggestions);
    if (!sugg.items.empty())
    {
        if (m_SuggestionIndex + 1 < sugg.items.size())
        {
            ++m_SuggestionIndex;
        }
        ClampSuggestionScroll(sugg.items.size());
        return;
    }
    auto recalled = m_Buffer.HistoryNext();
    if (recalled.has_value())
    {
        m_Buffer.SetInputLine(std::move(*recalled));
        m_SuggestionIndex = 0;
        m_SuggestionScroll = 0;
    }
}

void Console::OnLeft()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnLeft();
}

void Console::OnRight()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnRight();
}

void Console::OnHome()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnHome();
}

void Console::OnEnd()
{
    if (m_State == State::Closed)
        return;
    m_Buffer.OnEnd();
}

void Console::OnEscape()
{
    if (m_State == State::Closed)
        return;
    Close();
}

void Console::OnScroll(double yoffset)
{
    if (m_State == State::Closed)
        return;
    // Wheel up (positive yoffset) reveals older lines.
    constexpr int LINES_PER_NOTCH = 3;
    m_Buffer.Scroll(static_cast<int>(yoffset) * LINES_PER_NOTCH);
}

void Console::Submit(std::string_view line)
{
    // Echo the submitted line in dim color so users can see what ran.
    m_Buffer.Print("> " + std::string(line), glm::vec3(0.65f, 0.65f, 0.75f));

    auto tokens = Tokenize(line);
    if (tokens.empty())
    {
        return;
    }
    std::string verb(tokens.front());
    auto* cmd = m_Registry.Lookup(verb);
    if (cmd == nullptr)
    {
        m_Buffer.PrintError("Unknown command: " + verb + " (type 'help')");
        return;
    }
    std::span<const std::string_view> args(tokens.data() + 1, tokens.size() - 1);
    cmd->handler(args, *this);
}

// ============================================================================
// Console::Render - overlay drawing
// ============================================================================

void Console::Render(IRenderer& renderer, int screenWidth, int screenHeight)
{
    if (m_State == State::Closed)
    {
        return;
    }

    IRenderer::PerspectiveSuspendGuard guard(renderer);

    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    const glm::mat4 ui = glm::ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);
    renderer.SetProjection(ui);

    // ---- Translucent backdrop ----
    // Half mode: top 50% of the screen, world visible underneath. Full mode:
    // covers the entire framebuffer for an immersive ops session.
    const float overlayH = (m_State == State::Full) ? h : h * 0.5f;
    renderer.DrawColoredRect(
        glm::vec2(0.0f, 0.0f), glm::vec2(w, overlayH), glm::vec4(0.05f, 0.05f, 0.08f, 0.85f));

    // Thin separator at the bottom of the overlay - only meaningful in Half
    // mode, where it visually divides the console from the world below.
    if (m_State == State::Half)
    {
        renderer.DrawColoredRect(glm::vec2(0.0f, overlayH - 1.0f),
                                 glm::vec2(w, 1.0f),
                                 glm::vec4(0.5f, 0.6f, 0.9f, 0.8f));
    }

    constexpr float TEXT_SCALE = 0.75f;
    constexpr float LEFT_PAD = 8.0f;
    constexpr float TOP_PAD = 6.0f;
    constexpr float BOTTOM_PAD = 6.0f;
    constexpr float LINE_GAP_FACTOR = 1.4f;

    const float ascent = renderer.GetTextAscent(TEXT_SCALE);
    const float lineH = ascent * LINE_GAP_FACTOR;

    // IRenderer::DrawText takes y as the glyph baseline (see IRenderer.h).

    // ---- Prompt + input + cursor at the bottom of the overlay ----
    const float promptBaseline = overlayH - BOTTOM_PAD;
    const std::string prompt = "> " + m_Buffer.Input();
    renderer.DrawText(prompt, glm::vec2(LEFT_PAD, promptBaseline), TEXT_SCALE, glm::vec3(1.0f));

    // Cursor: blinking vertical bar occupying the same vertical band as the
    // text - from baseline-ascent (top) to baseline (bottom).
    const float blinkPeriod = 1.0f;
    const float t = static_cast<float>(glfwGetTime());
    const bool cursorOn = std::fmod(t, blinkPeriod) < blinkPeriod * 0.5f;
    if (cursorOn)
    {
        const std::string preCursor = "> " + m_Buffer.Input().substr(0, m_Buffer.CursorPos());
        const float cursorX = LEFT_PAD + renderer.GetTextWidth(preCursor, TEXT_SCALE);
        renderer.DrawColoredRect(glm::vec2(cursorX, promptBaseline - ascent),
                                 glm::vec2(1.5f, ascent),
                                 glm::vec4(1.0f, 1.0f, 1.0f, 0.9f));
    }

    // ---- Scrollback (bottom-up, above the prompt) ----
    // Scrollback baselines stack upward from just above the prompt's top edge.
    const float scrollBottom = promptBaseline - lineH;
    const float scrollTop = TOP_PAD + ascent;
    const int visibleLines = std::max(0, static_cast<int>((scrollBottom - scrollTop) / lineH));

    const auto& lines = m_Buffer.Lines();
    if (!lines.empty() && visibleLines > 0)
    {
        const int total = static_cast<int>(lines.size());
        // The newest line that's visible after scroll offset.
        const int newestVisible = std::max(0, total - 1 - m_Buffer.ScrollOffset());
        for (int i = 0; i < visibleLines; ++i)
        {
            const int idx = newestVisible - i;
            if (idx < 0)
            {
                break;
            }
            const float y = scrollBottom - i * lineH;
            renderer.DrawText(lines[static_cast<std::size_t>(idx)].text,
                              glm::vec2(LEFT_PAD, y),
                              TEXT_SCALE,
                              lines[static_cast<std::size_t>(idx)].color);
        }
    }

    // ---- Autocomplete dropdown ----
    // Sits below the input in Half mode (extending into the world) and above
    // the input in Full mode (no room below; floats up over scrollback).
    // Drawn LAST so its backdrop covers any scrollback line it overlaps -
    // otherwise scrollback text in fullscreen mode shows through the box.
    {
        m_LastDropdown.visible = false;
        const auto sugg = ComputeSuggestions(kMaxSuggestions);
        if (!sugg.items.empty())
        {
            // Match the input handlers' clamping in case the item count
            // shrank between the last input event and this frame.
            ClampSuggestionScroll(sugg.items.size());

            const std::size_t totalItems = sugg.items.size();
            const std::size_t visibleRows = std::min(kMaxVisibleSuggestions, totalItems);

            // Width is sized to the widest *visible* item. Stable enough -
            // recomputed every frame so it matches the rows actually shown.
            float widest = 0.0f;
            for (std::size_t i = 0; i < visibleRows; ++i)
            {
                const std::size_t idx = m_SuggestionScroll + i;
                widest = std::max(widest, renderer.GetTextWidth(sugg.items[idx], TEXT_SCALE));
            }
            constexpr float DROPDOWN_PAD = 6.0f;
            constexpr float DROPDOWN_GAP = 4.0f;
            constexpr float SCROLLBAR_W = 4.0f;
            const bool needsScrollbar = (totalItems > visibleRows);
            const float scrollbarReserve = needsScrollbar ? (SCROLLBAR_W + 4.0f) : 0.0f;
            const float maxBoxW = std::max(0.0f, w - 2.0f * LEFT_PAD);
            const float boxW = std::min(widest + 2.0f * LEFT_PAD + scrollbarReserve, maxBoxW);
            const float boxH = static_cast<float>(visibleRows) * lineH + 2.0f * DROPDOWN_PAD;
            // Anchor horizontally to the start of the current word in the
            // input. `wordStart` is the byte offset in the input where the
            // partial token being completed begins (0 for the verb,
            // post-space for arg completion). We measure the prompt + input
            // up to that offset using the same text-width metric the prompt
            // is rendered with, so the dropdown's left edge sits exactly
            // under where the user is typing.
            const std::string anchorPrefix = "> " + m_Buffer.Input().substr(0, sugg.wordStart);
            const float anchorOffset = renderer.GetTextWidth(anchorPrefix, TEXT_SCALE);
            float boxX = LEFT_PAD + anchorOffset;
            // If the dropdown would clip past the right edge, slide it left
            // so it stays fully on-screen (but never further left than the
            // standard LEFT_PAD).
            const float rightLimit = w - LEFT_PAD - boxW;
            if (boxX > rightLimit)
            {
                boxX = std::max(LEFT_PAD, rightLimit);
            }
            const float boxY = (m_State == State::Half)
                                   ? promptBaseline + DROPDOWN_PAD + DROPDOWN_GAP
                                   : promptBaseline - ascent - boxH - DROPDOWN_GAP;
            // Opaque-ish backdrop so scrollback drawn underneath doesn't leak
            // through.
            renderer.DrawColoredRect(glm::vec2(boxX, boxY),
                                     glm::vec2(boxW, boxH),
                                     glm::vec4(0.05f, 0.05f, 0.08f, 0.95f));
            // Highlight the selected entry, but only when the selection is
            // currently inside the visible window. (If the selected item
            // scrolled off-screen, ClampSuggestionScroll above keeps it in
            // view, so this branch holds in practice.)
            if (m_SuggestionIndex >= m_SuggestionScroll &&
                m_SuggestionIndex < m_SuggestionScroll + visibleRows)
            {
                const std::size_t selectedRow = m_SuggestionIndex - m_SuggestionScroll;
                const float highlightY =
                    boxY + DROPDOWN_PAD + static_cast<float>(selectedRow) * lineH;
                renderer.DrawColoredRect(glm::vec2(boxX, highlightY),
                                         glm::vec2(boxW, lineH),
                                         glm::vec4(0.20f, 0.30f, 0.55f, 0.65f));
            }
            for (std::size_t i = 0; i < visibleRows; ++i)
            {
                const std::size_t idx = m_SuggestionScroll + i;
                const float baseline = boxY + DROPDOWN_PAD + ascent + static_cast<float>(i) * lineH;
                renderer.DrawText(sugg.items[idx],
                                  glm::vec2(boxX + LEFT_PAD, baseline),
                                  TEXT_SCALE,
                                  glm::vec3(1.0f));
            }

            // Scrollbar showing the current window's position in the full
            // list. Only drawn when the list overflows the visible window.
            if (needsScrollbar)
            {
                const float trackX = boxX + boxW - SCROLLBAR_W - 2.0f;
                const float trackY = boxY + DROPDOWN_PAD;
                const float trackH = boxH - 2.0f * DROPDOWN_PAD;
                renderer.DrawColoredRect(glm::vec2(trackX, trackY),
                                         glm::vec2(SCROLLBAR_W, trackH),
                                         glm::vec4(0.15f, 0.15f, 0.20f, 0.6f));
                const float thumbH =
                    trackH * static_cast<float>(visibleRows) / static_cast<float>(totalItems);
                const float thumbY = trackY + trackH * static_cast<float>(m_SuggestionScroll) /
                                                  static_cast<float>(totalItems);
                renderer.DrawColoredRect(glm::vec2(trackX, thumbY),
                                         glm::vec2(SCROLLBAR_W, std::max(thumbH, 6.0f)),
                                         glm::vec4(0.55f, 0.62f, 0.80f, 0.85f));
            }

            // Cache geometry for the next frame's mouse hit tests.
            m_LastDropdown.x = boxX;
            m_LastDropdown.y = boxY;
            m_LastDropdown.w = boxW;
            m_LastDropdown.h = boxH;
            m_LastDropdown.rowH = lineH;
            m_LastDropdown.padTop = DROPDOWN_PAD;
            m_LastDropdown.topRow = m_SuggestionScroll;
            m_LastDropdown.visibleRows = visibleRows;
            m_LastDropdown.totalItems = totalItems;
            m_LastDropdown.visible = true;
        }
    }
}

void Console::OnMouseHover(double mouseX, double mouseY)
{
    if (m_State == State::Closed || !m_LastDropdown.visible)
    {
        return;
    }
    if (mouseX < m_LastDropdown.x || mouseX > m_LastDropdown.x + m_LastDropdown.w ||
        mouseY < m_LastDropdown.y || mouseY > m_LastDropdown.y + m_LastDropdown.h)
    {
        return;
    }
    const float relY = static_cast<float>(mouseY) - (m_LastDropdown.y + m_LastDropdown.padTop);
    if (relY < 0.0f)
    {
        return;
    }
    const int row = static_cast<int>(relY / m_LastDropdown.rowH);
    if (row < 0 || row >= static_cast<int>(m_LastDropdown.visibleRows))
    {
        return;
    }
    const std::size_t itemIdx = m_LastDropdown.topRow + static_cast<std::size_t>(row);
    if (itemIdx < m_LastDropdown.totalItems)
    {
        m_SuggestionIndex = itemIdx;
    }
}

bool Console::OnMouseClick(double mouseX, double mouseY)
{
    if (m_State == State::Closed || !m_LastDropdown.visible)
    {
        return false;
    }
    if (mouseX < m_LastDropdown.x || mouseX > m_LastDropdown.x + m_LastDropdown.w ||
        mouseY < m_LastDropdown.y || mouseY > m_LastDropdown.y + m_LastDropdown.h)
    {
        return false;
    }
    const float relY = static_cast<float>(mouseY) - (m_LastDropdown.y + m_LastDropdown.padTop);
    if (relY < 0.0f)
    {
        return false;
    }
    const int row = static_cast<int>(relY / m_LastDropdown.rowH);
    if (row < 0 || row >= static_cast<int>(m_LastDropdown.visibleRows))
    {
        return false;
    }
    const std::size_t itemIdx = m_LastDropdown.topRow + static_cast<std::size_t>(row);
    if (itemIdx >= m_LastDropdown.totalItems)
    {
        return false;
    }
    m_SuggestionIndex = itemIdx;
    OnTab();
    return true;
}

bool Console::TryScrollDropdown(double mouseX, double mouseY, double yoffset)
{
    if (m_State == State::Closed || !m_LastDropdown.visible)
    {
        return false;
    }
    if (mouseX < m_LastDropdown.x || mouseX > m_LastDropdown.x + m_LastDropdown.w ||
        mouseY < m_LastDropdown.y || mouseY > m_LastDropdown.y + m_LastDropdown.h)
    {
        return false;
    }
    if (m_LastDropdown.totalItems <= m_LastDropdown.visibleRows)
    {
        // Cursor is over the box but the list fits without scrolling.
        // Still consume the wheel so it doesn't leak into scrollback.
        return true;
    }
    constexpr int LINES_PER_NOTCH = 2;
    const int delta = -static_cast<int>(yoffset) * LINES_PER_NOTCH;  // wheel up -> earlier rows
    const std::size_t maxScroll = m_LastDropdown.totalItems - m_LastDropdown.visibleRows;
    if (delta < 0)
    {
        const std::size_t step = static_cast<std::size_t>(-delta);
        m_SuggestionScroll = (step >= m_SuggestionScroll) ? 0 : m_SuggestionScroll - step;
    }
    else if (delta > 0)
    {
        m_SuggestionScroll =
            std::min(m_SuggestionScroll + static_cast<std::size_t>(delta), maxScroll);
    }
    return true;
}
