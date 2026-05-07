#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Game;
class IRenderer;
class Console;

/**
 * @class ConsoleBuffer
 * @brief Owns the developer console's scrollback ring, input line, and command history.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Pure data layer with no GLFW or renderer dependencies, so unit tests can
 * exercise it without a graphics context. The Console class composes one of
 * these and threads input/output through it.
 */
class ConsoleBuffer
{
public:
    /// Maximum number of scrollback lines retained. Older lines are dropped.
    static constexpr std::size_t MAX_LINES = 256;

    /// Maximum number of submitted commands retained for Up/Down recall.
    static constexpr std::size_t MAX_HISTORY = 64;

    /// One scrollback line: text + display color.
    struct Line
    {
        std::string text;
        glm::vec3 color{1.0f, 1.0f, 1.0f};
    };

    /// Append an info-colored line.
    void Print(std::string text, glm::vec3 color = glm::vec3(1.0f));

    /// Append a red error line.
    void PrintError(std::string text);

    /// Drop all scrollback (does not clear input or history).
    void Clear();

    // ---------------- Input line manipulation ----------------

    /// Insert a printable character at the cursor.
    void OnChar(std::uint32_t codepoint);

    /// Erase the character before the cursor (no-op if at start).
    void OnBackspace();

    /// Erase the character at the cursor (no-op if at end).
    void OnDelete();

    /// Move cursor one position left (clamped at 0).
    void OnLeft();

    /// Move cursor one position right (clamped at length).
    void OnRight();

    /// Move cursor to start of input.
    void OnHome();

    /// Move cursor to end of input.
    void OnEnd();

    /// Submit and clear the input line. Returns the submitted text and
    /// resets the history navigation index.
    std::string OnEnter();

    /// Replace the input line with @p text, cursor at end. Used by history
    /// navigation and tab completion.
    void SetInputLine(std::string text);

    // ---------------- History ----------------

    /// Record a submitted command into history (capped at MAX_HISTORY).
    /// Empty commands and exact duplicates of the most recent entry are skipped.
    void RecordHistory(std::string command);

    /// Walk one step back through history. Returns the recalled string, or
    /// std::nullopt if there is nothing further back.
    std::optional<std::string> HistoryPrev();

    /// Walk one step forward through history. Returns the next entry, an
    /// empty string when leaving history (back to a fresh prompt), or
    /// std::nullopt if not currently navigating history.
    std::optional<std::string> HistoryNext();

    /// Forget the current history navigation index (call when input is
    /// modified by typing or by submitting).
    void ResetHistoryIndex();

    // ---------------- Scrollback navigation ----------------

    /// Adjust scroll offset by @p deltaLines. Positive scrolls toward older
    /// lines, negative toward newer. Clamped to [0, max].
    void Scroll(int deltaLines);

    /// Pin scroll to the bottom of the buffer.
    void ResetScroll();

    // ---------------- Read-only accessors ----------------

    [[nodiscard]] const std::deque<Line>& Lines() const { return m_Lines; }
    [[nodiscard]] const std::string& Input() const { return m_Input; }
    [[nodiscard]] std::size_t CursorPos() const { return m_CursorPos; }
    [[nodiscard]] int ScrollOffset() const { return m_ScrollOffset; }
    [[nodiscard]] const std::vector<std::string>& History() const { return m_History; }
    [[nodiscard]] std::optional<std::size_t> HistoryIndex() const { return m_HistoryIdx; }

private:
    std::deque<Line> m_Lines;
    std::string m_Input;
    std::size_t m_CursorPos = 0;
    std::vector<std::string> m_History;
    std::optional<std::size_t> m_HistoryIdx;
    int m_ScrollOffset = 0;
};

/**
 * @class ConsoleCommandRegistry
 * @brief Maps command names to handler functions for the developer console.
 * @ingroup Core
 *
 * Names are stored in a std::map for stable alphabetical ordering, which
 * gives `help` a predictable listing and tab completion stable cycle order.
 */
class ConsoleCommandRegistry
{
public:
    using Handler = std::function<void(std::span<const std::string_view> args, Console& console)>;

    struct Command
    {
        std::string name;
        std::string description;
        Handler handler;
        /// Optional shorter / alternate spellings that resolve to this same
        /// handler. Lookup checks aliases when the canonical name doesn't
        /// match, and tab-completion offers them alongside canonical names.
        std::vector<std::string> aliases;
    };

    /// Register or replace a command. Empty names are rejected.
    /// @p aliases are alternate names that resolve to the same handler.
    /// Aliases are not stored as separate commands in the map; they're
    /// kept on the canonical entry so `help` can show them inline.
    void Register(std::string name,
                  std::string description,
                  Handler handler,
                  std::vector<std::string> aliases = {});

    /// Look up a command by canonical name or by alias. Returns nullptr if
    /// no match. Canonical names are O(log n); aliases are a linear fallback.
    [[nodiscard]] const Command* Lookup(std::string_view name) const;

    /// All command names (canonical + aliases) whose key starts with
    /// @p prefix, in alphabetical order. Empty prefix returns every name.
    [[nodiscard]] std::vector<std::string> MatchPrefix(std::string_view prefix) const;

    /// Read access to the underlying ordered map.
    [[nodiscard]] const std::map<std::string, Command>& All() const { return m_Commands; }

private:
    std::map<std::string, Command> m_Commands;
};

/**
 * @class Console
 * @brief In-game developer REPL toggled with `~`.
 * @ingroup Core
 *
 * The Console binds a ConsoleBuffer + ConsoleCommandRegistry to a Game and
 * provides input event hooks and an overlay renderer. Command handlers
 * receive a Console& so they can read game state via GetGame() and emit
 * output via Buffer().Print(...).
 *
 * Console is an authorised mutator of Game state and is declared a friend
 * of Game so handlers (defined in ConsoleCommands.cpp) can directly reach
 * private members like m_Player, m_GameState, m_TimeManager, m_Tilemap and
 * m_NPCs without forcing those onto Game's public API.
 */
class Console
{
public:
    /// Construct, take a Game reference, and register the default command set.
    explicit Console(Game& game);

    [[nodiscard]] bool IsOpen() const { return m_Open; }
    void Toggle();
    void Open();
    void Close();

    // ---------------- Input event entry points ----------------

    /// GLFW char callback path. Filters out the toggle key glyphs (`~`, `\``)
    /// so opening the console doesn't insert the toggle character.
    void OnChar(std::uint32_t codepoint);

    void OnEnter();
    void OnBackspace();
    void OnDelete();
    void OnTab();
    void OnUp();
    void OnDown();
    void OnLeft();
    void OnRight();
    void OnHome();
    void OnEnd();
    void OnEscape();
    void OnScroll(double yoffset);

    /// Parse and execute a complete command line. Public for testability.
    /// Empty/whitespace-only input is a no-op. Unknown verbs print an error.
    void Submit(std::string_view line);

    /// Render the translucent overlay. Caller passes the framebuffer size;
    /// this method installs an orthographic projection internally and
    /// suspends perspective via PerspectiveSuspendGuard.
    void Render(IRenderer& renderer, int screenWidth, int screenHeight);

    /// Split @p line on ASCII whitespace runs into views into @p line.
    /// The caller must keep @p line alive for the views' lifetime. Public
    /// and static so unit tests can exercise tokenization directly.
    [[nodiscard]] static std::vector<std::string_view> Tokenize(std::string_view line);

    // ---------------- Accessors used by command handlers ----------------

    [[nodiscard]] ConsoleBuffer& Buffer() { return m_Buffer; }
    [[nodiscard]] const ConsoleBuffer& Buffer() const { return m_Buffer; }
    [[nodiscard]] Game& GetGame() { return m_Game; }
    [[nodiscard]] const ConsoleCommandRegistry& Registry() const { return m_Registry; }

private:
    /// Reset the multi-press tab cycle state.
    void ResetTabState();

    /// Wire the eight built-in commands. Defined in ConsoleCommands.cpp.
    void RegisterDefaultCommands();

    Game& m_Game;
    ConsoleBuffer m_Buffer;
    ConsoleCommandRegistry m_Registry;
    bool m_Open = false;

    // ---------------- Tab completion cycle state ----------------
    bool m_TabActive = false;               ///< True after first Tab, until any other key
    std::vector<std::string> m_TabMatches;  ///< Captured matches when cycle began
    int m_TabIndex = -1;                    ///< -1 = showing common prefix; >=0 = matches[i]
    std::string m_TabBase;                  ///< Original input when cycle began
};
