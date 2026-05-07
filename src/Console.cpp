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
                                      std::vector<std::string> aliases)
{
    if (name.empty())
    {
        return;
    }
    Command cmd{name, std::move(description), std::move(handler), std::move(aliases)};
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

std::vector<std::string> ConsoleCommandRegistry::MatchPrefix(std::string_view prefix) const
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

namespace
{
/// Longest common prefix of a non-empty list of strings.
std::string LongestCommonPrefix(const std::vector<std::string>& strings)
{
    if (strings.empty())
    {
        return {};
    }
    std::string prefix = strings.front();
    for (std::size_t i = 1; i < strings.size(); ++i)
    {
        const std::string& s = strings[i];
        std::size_t j = 0;
        while (j < prefix.size() && j < s.size() && prefix[j] == s[j])
        {
            ++j;
        }
        prefix.resize(j);
        if (prefix.empty())
        {
            break;
        }
    }
    return prefix;
}
}  // namespace

Console::Console(Game& game)
    : m_Game(game)
{
    RegisterDefaultCommands();
}

void Console::Toggle()
{
    if (m_Open)
    {
        Close();
    }
    else
    {
        Open();
    }
}

void Console::Open()
{
    m_Open = true;
    m_Buffer.ResetScroll();
    ResetTabState();
}

void Console::Close()
{
    m_Open = false;
    ResetTabState();
}

void Console::ResetTabState()
{
    m_TabActive = false;
    m_TabMatches.clear();
    m_TabIndex = -1;
    m_TabBase.clear();
}

void Console::OnChar(std::uint32_t codepoint)
{
    if (!m_Open)
    {
        return;
    }
    // Suppress toggle-key glyphs across keyboard layouts. Game::CharCallback
    // catches the case where the GRAVE_ACCENT physical key produces a char
    // while still held, but on layouts where the same key is a dead key
    // (German / French / Polish: ^), the char event fires only when the
    // next key is pressed - by which time the toggle key is released. This
    // codepoint filter catches that delayed emission.
    //   US layout:        ` (grave) and ~ (Shift+grave)
    //   German / Polish:  ^ (caret) and degree symbol (Shift+caret)
    if (codepoint == '`' || codepoint == '~' || codepoint == '^' || codepoint == 0x00B0)
    {
        return;
    }
    m_Buffer.OnChar(codepoint);
    ResetTabState();
}

void Console::OnEnter()
{
    if (!m_Open)
    {
        return;
    }
    std::string line = m_Buffer.OnEnter();
    ResetTabState();
    if (line.empty())
    {
        return;
    }
    m_Buffer.RecordHistory(line);
    Submit(line);
}

void Console::OnBackspace()
{
    if (!m_Open)
        return;
    m_Buffer.OnBackspace();
    ResetTabState();
}

void Console::OnDelete()
{
    if (!m_Open)
        return;
    m_Buffer.OnDelete();
    ResetTabState();
}

void Console::OnTab()
{
    if (!m_Open)
    {
        return;
    }
    if (!m_TabActive)
    {
        // Only complete the first whitespace-separated token (the command name).
        // If the input already has a space, completion would need argument
        // awareness - out of scope for the MVP, so do nothing.
        const std::string& input = m_Buffer.Input();
        if (input.find(' ') != std::string::npos)
        {
            return;
        }
        m_TabBase = input;
        m_TabMatches = m_Registry.MatchPrefix(m_TabBase);
        if (m_TabMatches.empty())
        {
            return;
        }
        if (m_TabMatches.size() == 1)
        {
            m_Buffer.SetInputLine(m_TabMatches.front());
            m_TabActive = true;
            m_TabIndex = 0;
            return;
        }
        // Multiple matches: prefer the longest common prefix if it extends
        // past what the user has typed.
        std::string lcp = LongestCommonPrefix(m_TabMatches);
        if (lcp.size() > m_TabBase.size())
        {
            m_Buffer.SetInputLine(lcp);
            m_TabActive = true;
            m_TabIndex = -1;  // next Tab will pick matches[0]
            return;
        }
        // Common prefix is no help; jump to the first full match.
        m_Buffer.SetInputLine(m_TabMatches.front());
        m_TabActive = true;
        m_TabIndex = 0;
        return;
    }
    // Already cycling - advance to the next match.
    if (m_TabMatches.empty())
    {
        return;
    }
    m_TabIndex = (m_TabIndex + 1) % static_cast<int>(m_TabMatches.size());
    m_Buffer.SetInputLine(m_TabMatches[static_cast<std::size_t>(m_TabIndex)]);
}

void Console::OnUp()
{
    if (!m_Open)
        return;
    auto recalled = m_Buffer.HistoryPrev();
    if (recalled.has_value())
    {
        m_Buffer.SetInputLine(std::move(*recalled));
    }
    ResetTabState();
}

void Console::OnDown()
{
    if (!m_Open)
        return;
    auto recalled = m_Buffer.HistoryNext();
    if (recalled.has_value())
    {
        m_Buffer.SetInputLine(std::move(*recalled));
    }
    ResetTabState();
}

void Console::OnLeft()
{
    if (!m_Open)
        return;
    m_Buffer.OnLeft();
    ResetTabState();
}

void Console::OnRight()
{
    if (!m_Open)
        return;
    m_Buffer.OnRight();
    ResetTabState();
}

void Console::OnHome()
{
    if (!m_Open)
        return;
    m_Buffer.OnHome();
    ResetTabState();
}

void Console::OnEnd()
{
    if (!m_Open)
        return;
    m_Buffer.OnEnd();
    ResetTabState();
}

void Console::OnEscape()
{
    if (!m_Open)
        return;
    Close();
}

void Console::OnScroll(double yoffset)
{
    if (!m_Open)
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
    if (!m_Open)
    {
        return;
    }

    IRenderer::PerspectiveSuspendGuard guard(renderer);

    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    const glm::mat4 ui = glm::ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);
    renderer.SetProjection(ui);

    // ---- Translucent backdrop (top half of the screen) ----
    const float overlayH = h * 0.5f;
    renderer.DrawColoredRect(
        glm::vec2(0.0f, 0.0f), glm::vec2(w, overlayH), glm::vec4(0.05f, 0.05f, 0.08f, 0.85f));

    // Thin separator at the bottom of the overlay.
    renderer.DrawColoredRect(
        glm::vec2(0.0f, overlayH - 1.0f), glm::vec2(w, 1.0f), glm::vec4(0.5f, 0.6f, 0.9f, 0.8f));

    constexpr float TEXT_SCALE = 0.75f;
    constexpr float LEFT_PAD = 8.0f;
    constexpr float TOP_PAD = 6.0f;
    constexpr float BOTTOM_PAD = 6.0f;
    constexpr float LINE_GAP_FACTOR = 1.4f;

    const float ascent = renderer.GetTextAscent(TEXT_SCALE);
    const float lineH = ascent * LINE_GAP_FACTOR;

    // IRenderer::DrawText treats `position.y` as the glyph baseline (despite
    // the header doc's "top-left" wording - see OpenGLRenderer::DrawText
    // computing `ypos = y - bearing.y * scale`). Glyphs extend upward from
    // the baseline, so we line up text and the cursor rect against the
    // baseline rather than against an apparent top-left.

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
}
