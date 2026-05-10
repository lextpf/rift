#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
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
    /// Sized large enough to hold a full per-frame draw-call trace dump
    /// (`renderer.trace dump`) without the head being evicted as later
    /// events scroll in. ~50 KB at one Line per ~50 chars + small overhead;
    /// fine for desktop. For multi-frame traces, prefer `renderer.trace file`
    /// which bypasses the scrollback entirely.
    static constexpr std::size_t MAX_LINES = 8192;

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

    /// Delete the word before the cursor: first eat any contiguous spaces
    /// immediately preceding the cursor, then eat one contiguous run of
    /// non-space characters. Repeated calls walk back word-by-word until the
    /// line is empty. Bound to Ctrl+Backspace.
    void OnBackspaceWord();

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

    /// Returns the set of valid values for the @p argIndex'th positional
    /// argument (0 = first arg after the verb). Used by the autocomplete
    /// dropdown to suggest parameter values; return an empty vector when no
    /// completion is available for that slot. The callback is invoked with
    /// arbitrary indices as the user types, so it must handle out-of-range
    /// indices gracefully.
    using ArgCompletionProvider = std::function<std::vector<std::string>(std::size_t argIndex)>;

    struct Command
    {
        std::string name;
        std::string description;
        Handler handler;
        /// Optional shorter / alternate spellings that resolve to this same
        /// handler. Lookup checks aliases when the canonical name doesn't
        /// match, and tab-completion offers them alongside canonical names.
        std::vector<std::string> aliases;
        /// Optional callback that supplies dropdown suggestions for positional
        /// arguments (e.g. enum values). May be null.
        ArgCompletionProvider argCompletions;
    };

    /// Register or replace a command. Empty names are rejected.
    /// @p aliases are alternate names that resolve to the same handler.
    /// Aliases are not stored as separate commands in the map; they're
    /// kept on the canonical entry so `help` can show them inline.
    /// @p argCompletions provides per-arg autocomplete values; pass nullptr
    /// (the default) when arguments have no canned completions.
    void Register(std::string name,
                  std::string description,
                  Handler handler,
                  std::vector<std::string> aliases = {},
                  ArgCompletionProvider argCompletions = nullptr);

    /// Look up a command by canonical name or by alias. Returns nullptr if
    /// no match. Canonical names are O(log n); aliases are a linear fallback.
    [[nodiscard]] const Command* Lookup(std::string_view name) const;

    /// All command names (canonical + aliases) whose key starts with
    /// @p prefix, in alphabetical order. Empty prefix returns every name.
    /// @p maxCount caps the result length (the alphabetically-earliest matches
    /// are kept). Used by the autocomplete dropdown to fetch up to N hints.
    [[nodiscard]] std::vector<std::string> MatchPrefix(
        std::string_view prefix,
        std::size_t maxCount = (std::numeric_limits<std::size_t>::max)()) const;

    /// Read access to the underlying ordered map.
    [[nodiscard]] const std::map<std::string, Command>& All() const { return m_Commands; }

private:
    std::map<std::string, Command> m_Commands;
};

/**
 * @class Console
 * @brief In-game developer REPL toggled with F12.
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
    /// Visibility / size state. The toggle hotkey advances `Closed -> Half ->
    /// Full -> Closed`. `Half` is the legacy top-50% overlay (world visible
    /// underneath); `Full` covers the entire framebuffer for longer ops
    /// sessions.
    enum class State : std::uint8_t
    {
        Closed,
        Half,
        Full
    };

    /// Construct, take a Game reference, and register the default command set.
    explicit Console(Game& game);

    /// True when the overlay is in Half or Full state.
    [[nodiscard]] bool IsOpen() const { return m_State != State::Closed; }
    /// True when the overlay covers the full framebuffer.
    [[nodiscard]] bool IsFullscreen() const { return m_State == State::Full; }
    /// Current visibility/size state.
    [[nodiscard]] State GetState() const { return m_State; }
    /// Advance Closed -> Half -> Full -> Closed.
    void Toggle();
    /// Open the console to the default visible state.
    void Open();
    /// Close the overlay and stop consuming console input.
    void Close();

    // ---------------- Input event entry points ----------------

    /// GLFW char callback path. Inserts the typed glyph into the input buffer
    /// while the console is open; no-op otherwise.
    void OnChar(std::uint32_t codepoint);

    /// Execute the current line or accept the active autocomplete suggestion.
    void OnEnter();
    /// Delete one code unit before the cursor.
    void OnBackspace();
    /// Delete the word before the cursor.
    void OnBackspaceWord();
    /// Delete one code unit at the cursor.
    void OnDelete();
    /// Complete or cycle command suggestions.
    void OnTab();
    /// Navigate command history or suggestions upward.
    void OnUp();
    /// Navigate command history or suggestions downward.
    void OnDown();
    /// Move the cursor left.
    void OnLeft();
    /// Move the cursor right.
    void OnRight();
    /// Move the cursor to the start of the line.
    void OnHome();
    /// Move the cursor to the end of the line.
    void OnEnd();
    /// Close the console or clear suggestion state.
    void OnEscape();
    /// Scroll console history by the wheel delta.
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

    /// Mutable output/input buffer used by command handlers.
    [[nodiscard]] ConsoleBuffer& Buffer() { return m_Buffer; }
    /// Read-only output/input buffer for render and inspection paths.
    [[nodiscard]] const ConsoleBuffer& Buffer() const { return m_Buffer; }
    /// Game instance that owns this console.
    [[nodiscard]] Game& GetGame() { return m_Game; }
    /// Registered command table.
    [[nodiscard]] const ConsoleCommandRegistry& Registry() const { return m_Registry; }

    /// Session-scoped player-position bookmarks driven by `bookmark.set` /
    /// `bookmark.tp` / `bookmark.list`. Cleared on Console destruction; not
    /// persisted to disk.
    [[nodiscard]] std::unordered_map<std::string, glm::ivec2>& Bookmarks() { return m_Bookmarks; }
    [[nodiscard]] const std::unordered_map<std::string, glm::ivec2>& Bookmarks() const
    {
        return m_Bookmarks;
    }

    /// One round of suggestion computation. `items` holds the prefix-matched
    /// candidates (alphabetical, capped to the requested count). `wordStart`
    /// is the index in the input where the partial word begins, so callers
    /// can splice a chosen suggestion in: `input.substr(0, wordStart) + items[i]`.
    struct SuggestionResult
    {
        std::vector<std::string> items;
        std::size_t wordStart = 0;
    };

    /// Mouse cursor moved over the suggestion dropdown. If the cursor is
    /// inside the box, snap @c m_SuggestionIndex to the row under the cursor
    /// so hover-to-highlight matches what a click would commit.
    void OnMouseHover(double mouseX, double mouseY);

    /// Left-click at @p mouseX,mouseY. If the click landed inside the
    /// dropdown box, splice the clicked suggestion into the input (same path
    /// as Tab) and return true so the caller can swallow the click.
    bool OnMouseClick(double mouseX, double mouseY);

    /// Mouse wheel hit-routing: if the cursor sits over the dropdown and the
    /// suggestion list overflows the visible window, scroll the dropdown and
    /// return true. Caller falls through to scrollback navigation otherwise.
    bool TryScrollDropdown(double mouseX, double mouseY, double yoffset);

private:
    /// Wire the built-in command set. Defined in ConsoleCommands.cpp.
    void RegisterDefaultCommands();

    /// Compute the up-to-@p maxCount autocomplete suggestions for the current
    /// input line. Suggests command names while typing the verb, and falls
    /// back to the verb's `argCompletions` callback when typing positional
    /// arguments. Used by both the dropdown renderer and Tab completion so
    /// the visible list and the chosen completion stay in lockstep.
    [[nodiscard]] SuggestionResult ComputeSuggestions(std::size_t maxCount) const;

    /// Slide @c m_SuggestionScroll so @c m_SuggestionIndex stays inside the
    /// visible window, then clamp the scroll to a valid range. Called any
    /// time the index or item count changes.
    void ClampSuggestionScroll(std::size_t itemCount);

    /// Hard cap on suggestions so a degenerate prefix can't blow up the box.
    /// Larger than any realistic command list; raise if completion sources
    /// ever return more.
    static constexpr std::size_t kMaxSuggestions = 64;

    /// Maximum rows shown in the dropdown box at once. Items beyond this
    /// scroll behind the visible window.
    static constexpr std::size_t kMaxVisibleSuggestions = 8;

    Game& m_Game;
    ConsoleBuffer m_Buffer;
    ConsoleCommandRegistry m_Registry;
    State m_State = State::Closed;
    /// Index of the highlighted entry in the current dropdown (across the
    /// full item list, not just the visible window). Reset to 0 on any
    /// input modification; clamped to the suggestion count when read.
    std::size_t m_SuggestionIndex = 0;
    /// First visible row in the dropdown's sliding window. Adjusted via
    /// arrow-key navigation, mouse wheel, and ClampSuggestionScroll.
    std::size_t m_SuggestionScroll = 0;
    /// Geometry cached during Render() so input handlers (mouse hover,
    /// click, wheel) can hit-test the dropdown without their own copy of
    /// the layout math. Refreshed every frame; @c visible is false when
    /// the dropdown isn't drawn this frame.
    struct DropdownRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float rowH = 0.0f;
        float padTop = 0.0f;
        std::size_t topRow = 0;       ///< first visible item index
        std::size_t visibleRows = 0;  ///< rows currently drawn
        std::size_t totalItems = 0;   ///< full suggestion count
        bool visible = false;
    };
    DropdownRect m_LastDropdown;

    /// Session-scoped bookmark storage. Keyed by user-supplied name; value is
    /// the player's tile coordinates at the time of `bookmark.set`. Empty by
    /// default; not persisted across program runs.
    std::unordered_map<std::string, glm::ivec2> m_Bookmarks;
};

/// Pure transition function for the console toggle cycle. Exposed as a free
/// function (and made `constexpr`) so unit tests can validate the cycle
/// without constructing a Console + Game pair, which would require a GL
/// context per the test-suite constraints.
[[nodiscard]] constexpr Console::State NextConsoleState(Console::State s) noexcept
{
    switch (s)
    {
        case Console::State::Closed:
            return Console::State::Half;
        case Console::State::Half:
            return Console::State::Full;
        case Console::State::Full:
            return Console::State::Closed;
    }
    return Console::State::Closed;
}
