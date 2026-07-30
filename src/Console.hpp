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
    /**
     * @brief Maximum number of scrollback lines retained. Older lines are dropped.
     *
     * Sized to hold a normal per-frame draw-call trace dump
     * (`renderer.trace dump`) without the head being evicted as later events
     * scroll in. A dump covers only the most recent frame
     * (DrawTracer::LastFrameEvents) and prints one line per traced event.
     * DrawTracer caps a frame at 50000 events, so a pathological frame can
     * still overflow this and evict the head; 8192 covers a normal frame with
     * margin. Under ~1 MB when full at ~50 characters per line (Line struct
     * plus heap text); fine for desktop.
     */
    static constexpr std::size_t MAX_LINES = 8192;

    /// @brief Maximum number of submitted commands retained for Up/Down recall.
    static constexpr std::size_t MAX_HISTORY = 64;

    /// @brief One scrollback line: text + display color.
    struct Line
    {
        std::string text;
        glm::vec3 color{1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief Append a line and snap the view to the newest line.
     *
     * The scroll offset is reset to 0 on every call, so printed output always
     * pulls a scrolled-back view down to the bottom. A caller that wants a
     * different view position must re-scroll after printing (see
     * @ref Console::ScrollToOutputTop, which `help` uses).
     *
     * @param text   Line content.
     * @param color  RGB display color; the default is white.
     */
    void Print(std::string text, glm::vec3 color = glm::vec3(1.0f));

    /// @brief Append a red error line.
    void PrintError(std::string text);

    /// @brief Drop all scrollback (does not clear input or history).
    void Clear();

    /**
     * @brief Insert one printable ASCII character at the cursor.
     *
     * Codepoints outside U+0020..U+007E (non-ASCII glyphs, IME composition,
     * control codes, tab) are dropped without a diagnostic. Also forgets the
     * history navigation index.
     *
     * @param codepoint Unicode codepoint from the GLFW char callback.
     */
    void OnChar(std::uint32_t codepoint);

    /// @brief Erase the character before the cursor (no-op if at start).
    void OnBackspace();

    /**
     * @brief Delete the word before the cursor.
     *
     * Space and dot are both boundary characters. First eat any contiguous
     * boundary characters immediately preceding the cursor, then eat one
     * contiguous run of non-boundary characters. Repeated calls walk back
     * segment-by-segment until the line is empty, so a dotted verb collapses
     * one segment at a time:
     * `time.weather clear` -> `time.weather ` -> `time.` -> ``.
     * Bound to Ctrl+Backspace.
     */
    void OnBackspaceWord();

    /// @brief Erase the character at the cursor (no-op if at end).
    void OnDelete();

    /// @brief Move cursor one position left (clamped at 0).
    void OnLeft();

    /// @brief Move cursor one position right (clamped at length).
    void OnRight();

    /// @brief Move cursor to start of input.
    void OnHome();

    /// @brief Move cursor to end of input.
    void OnEnd();

    /**
     * @brief Submit and clear the input line.
     *
     * Returns the submitted text and resets the history navigation index.
     */
    std::string OnEnter();

    /**
     * @brief Replace the input line with @p text, cursor at end.
     *
     * Used by history navigation and tab completion.
     */
    void SetInputLine(std::string text);

    /**
     * @brief Record a submitted command into history (capped at MAX_HISTORY).
     *
     * Empty commands and exact duplicates of the most recent entry are skipped.
     */
    void RecordHistory(std::string command);

    /**
     * @brief Walk one step back through history.
     *
     * Returns the recalled string, or std::nullopt if there is nothing further
     * back.
     */
    std::optional<std::string> HistoryPrev();

    /**
     * @brief Walk one step forward through history.
     *
     * Returns the next entry, an empty string when leaving history (back to a
     * fresh prompt), or std::nullopt if not currently navigating history.
     */
    std::optional<std::string> HistoryNext();

    /**
     * @brief Forget the current history navigation index.
     *
     * Call when input is modified by typing or by submitting.
     */
    void ResetHistoryIndex();

    /**
     * @brief Adjust scroll offset by @p deltaLines.
     *
     * Positive scrolls toward older lines, negative toward newer. Clamped to
     * [0, Lines().size()]. The upper bound is the full line count, not
     * count-minus-visible-rows, so the view can be scrolled until only the
     * oldest line remains on the bottom row.
     */
    void Scroll(int deltaLines);

    /// @brief Pin scroll to the bottom of the buffer.
    void ResetScroll();

    /**
     * @brief Set the absolute scroll offset (lines up from the bottom), clamped
     * to [0, line count].
     *
     * Lets callers position the view at a specific point, e.g. the top of a
     * freshly printed block.
     */
    void ScrollTo(int offsetFromBottom);

    /// @name Observers
    /// @{
    /// @brief Scrollback lines, oldest first.
    [[nodiscard]] const std::deque<Line>& Lines() const { return m_Lines; }
    /// @brief Current (unsubmitted) input line.
    [[nodiscard]] const std::string& Input() const { return m_Input; }
    /// @brief Cursor position as a byte index into @ref Input, in [0, size()].
    [[nodiscard]] std::size_t CursorPos() const { return m_CursorPos; }
    /// @brief Lines scrolled up from the newest line; 0 means pinned to the bottom.
    [[nodiscard]] int ScrollOffset() const { return m_ScrollOffset; }
    /// @brief Recorded command history, oldest first.
    [[nodiscard]] const std::vector<std::string>& History() const { return m_History; }
    /// @brief Index into @ref History being walked, or nullopt when not navigating history.
    [[nodiscard]] std::optional<std::size_t> HistoryIndex() const { return m_HistoryIdx; }
    /// @}

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
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Names are stored in a std::map for stable alphabetical ordering, which
 * gives `help` a predictable listing and the autocomplete dropdown a stable
 * row order.
 */
class ConsoleCommandRegistry
{
public:
    using Handler = std::function<void(std::span<const std::string_view> args, Console& console)>;

    /**
     * @brief Returns the set of valid values for the @p argIndex'th positional
     * argument (0 = first arg after the verb).
     *
     * Used by the autocomplete dropdown to suggest parameter values; return an
     * empty vector when no completion is available for that slot. The callback
     * is invoked with arbitrary indices as the user types, so it must handle
     * out-of-range indices gracefully.
     *
     * @warning Called from @ref Console::Render once per frame for as long as
     * the dropdown is open (plus once per Tab / Up / Down), and it returns a
     * freshly built vector each time, so keep it cheap. The registry stores the
     * callable for the Console's lifetime; anything it captures must outlive
     * the Console.
     */
    using ArgCompletionProvider = std::function<std::vector<std::string>(std::size_t argIndex)>;

    /// @brief One registered command: canonical name, help text, handler, and optional extras.
    struct Command
    {
        std::string name;  ///< Canonical verb; also the map key.
        /**
         * @brief User-visible help text, by convention `<grammar> - summary`.
         *
         * `help` prints it verbatim after `name (aliases) - `, so it carries
         * the argument grammar for the command.
         */
        std::string description;
        Handler handler;  ///< Invoked with the argument tokens after the verb.
        /**
         * @brief Optional shorter / alternate spellings that resolve to this
         * same handler.
         *
         * Lookup checks aliases when the canonical name doesn't match, and
         * tab-completion offers them alongside canonical names.
         */
        std::vector<std::string> aliases;
        /**
         * @brief Optional callback that supplies dropdown suggestions for
         * positional arguments (e.g. enum values). May be null.
         */
        ArgCompletionProvider argCompletions;
    };

    /**
     * @brief Register or replace a command. Empty names are rejected.
     *
     * @p aliases are alternate names that resolve to the same handler. Aliases
     * are not stored as separate commands in the map; they're kept on the
     * canonical entry so `help` can show them inline. @p argCompletions
     * provides per-arg autocomplete values; pass nullptr (the default) when
     * arguments have no canned completions.
     */
    void Register(std::string name,
                  std::string description,
                  Handler handler,
                  std::vector<std::string> aliases = {},
                  ArgCompletionProvider argCompletions = nullptr);

    /**
     * @brief Attach (or replace) the argument-completion provider on an
     * already-registered command, looked up by canonical @p name.
     *
     * No-op if @p name isn't registered. Lets the default command set wire
     * completions for many commands in one place after registration instead of
     * threading a provider through every @ref Register call.
     */
    void SetArgCompletions(std::string_view name, ArgCompletionProvider argCompletions);

    /**
     * @brief Look up a command by canonical name or by alias.
     *
     * Returns nullptr if no match. Canonical names are O(log n); aliases are a
     * linear fallback. Matching is exact-case, unlike @ref MatchPrefix, so a
     * mixed-case verb that the dropdown suggested still fails to dispatch.
     */
    [[nodiscard]] const Command* Lookup(std::string_view name) const;

    /**
     * @brief All command names (canonical + aliases) whose key starts with
     * @p prefix, in alphabetical order.
     *
     * Empty prefix returns every name. @p maxCount caps the result length (the
     * alphabetically-earliest matches are kept). Used by the autocomplete
     * dropdown to fetch up to N hints. Prefix matching is ASCII
     * case-insensitive (`PA` matches `particles`) while the ordering is
     * byte-wise on the matched name; @ref Lookup, by contrast, is exact-case.
     */
    [[nodiscard]] std::vector<std::string> MatchPrefix(
        std::string_view prefix,
        std::size_t maxCount = (std::numeric_limits<std::size_t>::max)()) const;

    /**
     * @brief One entry returned by @ref MatchPrefixDetailed - the matched name
     * paired with the canonical command it resolves to.
     *
     * The matched @c name may be either a canonical command name or one of its
     * aliases. @c canonical is empty when @c name is itself the canonical name;
     * non-empty when @c name is an alias, in which case it holds the canonical
     * to which the alias resolves.
     */
    struct MatchEntry
    {
        std::string name;
        std::string canonical;
    };

    /**
     * @brief Like @ref MatchPrefix but tags each returned name with its
     * canonical command.
     *
     * Used by the autocomplete dropdown to render `alias -> canonical` hints so
     * the originating command for an alias is always visible.
     */
    [[nodiscard]] std::vector<MatchEntry> MatchPrefixDetailed(
        std::string_view prefix,
        std::size_t maxCount = (std::numeric_limits<std::size_t>::max)()) const;

    /// @brief Read access to the underlying ordered map.
    [[nodiscard]] const std::map<std::string, Command>& All() const { return m_Commands; }

private:
    std::map<std::string, Command> m_Commands;
};

/**
 * @class Console
 * @brief In-game developer REPL toggled with F12.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * The Console binds a ConsoleBuffer + ConsoleCommandRegistry to a Game and
 * provides input event hooks and an overlay renderer. Command handlers
 * receive a Console& so they can read game state via GetGame() and emit
 * output via Buffer().Print(...).
 *
 * Console is an authorised mutator of Game state and is declared a friend
 * of Game so handlers (defined in ConsoleCommands.cpp) can directly reach
 * private members like m_PlayerEntity, m_GameState, m_TimeManager, m_Tilemap
 * and m_World (the ECS world registry holding the player and every NPC)
 * without forcing those onto Game's public API. RegisterDefaultCommands is the
 * only function that reaches into Game this way; it packs the per-command set
 * into a fresh @ref CommandContext on every invocation.
 *
 * @par Visibility state machine
 * Two independent hotkeys drive the state, and neither one cycles all three
 * values. F12 (@ref Toggle) is open-or-close only: from `Closed` it always
 * lands in `Half`, and from `Half` or `Full` it goes straight to `Closed`, so
 * F12 can never reach `Full`. Tab (@ref ToggleFullscreen, only when the input
 * line is empty) swaps `Half` and `Full` and is a no-op while closed. `Half`
 * overlays the top 50% of the screen so the world is still visible; `Full`
 * covers the entire framebuffer for long ops sessions:
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     classDef closed fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef half   fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *     classDef full   fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *
 *     state "Closed" as C:::closed
 *     state "Half (top 50%)" as H:::half
 *     state "Full (entire frame)" as F:::full
 *
 *     [*] --> C
 *     C --> H: F12 (Toggle) / Open
 *     H --> C: F12 (Toggle) / Close / Esc
 *     F --> C: F12 (Toggle) / Close / Esc
 *     H --> F: Tab (ToggleFullscreen)
 *     F --> H: Tab (ToggleFullscreen) / Open
 * </pre>
 * @endhtmlonly
 *
 * @par Submission flow
 * Each input event hook (OnChar, OnBackspace, ...) mutates the ConsoleBuffer,
 * then OnEnter splits the line into tokens, looks up the verb in the
 * registry, and dispatches to its handler. Handlers print success/error
 * messages back through Buffer().Print() / Buffer().PrintError().
 *
 * @par Key routing
 * Three keys are overloaded, and two of the gates sit outside this class. Tab
 * is dispatched by Game::PumpConsoleKeys on whether the input line is empty;
 * Up/Down prefer the suggestion dropdown over history recall, so history is
 * reachable only when the input produces no suggestions; Enter never applies
 * the highlighted suggestion; Esc closes without touching dropdown state:
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart TD
 *     TAB["Tab"] --> TG{"input line empty?<br/>(Game::PumpConsoleKeys)"}
 *     TG -->|yes| TFS["ToggleFullscreen (Half &lt;-&gt; Full)"]
 *     TG -->|no| TAB2["OnTab: splice highlighted item"]
 *     UD["Up / Down"] --> UG{"suggestions non-empty?"}
 *     UG -->|yes| SEL["move dropdown selection"]
 *     UG -->|no| HIST["HistoryPrev / HistoryNext"]
 *     ENT["Enter"] --> SUB["RecordHistory then Submit<br/>(suggestion ignored)"]
 *     ESC["Esc"] --> CL["Close (dropdown state kept)"]
 * </pre>
 * @endhtmlonly
 *
 * @see ConsoleBuffer, ConsoleCommandRegistry, ConsoleCommands.hpp
 */
class Console
{
public:
    /**
     * @brief Visibility / size state.
     *
     * `Half` is the legacy top-50% overlay (world visible underneath); `Full`
     * covers the entire framebuffer for longer ops sessions. The enum order is
     * not a hotkey cycle: F12 (@ref Toggle) only moves between `Closed` and
     * `Half`, and Tab (@ref ToggleFullscreen) only swaps `Half` and `Full`.
     */
    enum class State : std::uint8_t
    {
        Closed,
        Half,
        Full
    };

    /// @brief Construct, take a Game reference, and register the default command set.
    explicit Console(Game& game);

    /// @brief True when the overlay is in Half or Full state.
    [[nodiscard]] bool IsOpen() const { return m_State != State::Closed; }
    /// @brief True when the overlay covers the full framebuffer.
    [[nodiscard]] bool IsFullscreen() const { return m_State == State::Full; }
    /// @brief Current visibility/size state.
    [[nodiscard]] State GetState() const { return m_State; }
    /**
     * @brief F12 hotkey: open the console (to Half) if closed, otherwise close it.
     *
     * Does not cycle through Full - that's @ref ToggleFullscreen via Tab.
     */
    void Toggle();
    /**
     * @brief Tab hotkey when the console is open and the input line is empty:
     * toggle between Half and Full.
     *
     * No-op when closed.
     */
    void ToggleFullscreen();
    /**
     * @brief Force the console to `Half` from any state and pin the scrollback
     * to the newest line.
     *
     * Called while `Full` this demotes the overlay to `Half`. No production or
     * test code calls it today; F12 opens the console through @ref Toggle.
     */
    void Open();
    /// @brief Close the overlay and stop consuming console input.
    void Close();

    /**
     * @brief GLFW char callback path.
     *
     * Inserts the typed glyph into the input buffer while the console is open;
     * no-op otherwise.
     */
    void OnChar(std::uint32_t codepoint);

    /**
     * @brief Submit the current input line; the highlighted suggestion is not
     * applied.
     *
     * Records the line in history and hands it to @ref Submit, then resets the
     * dropdown selection to the top. Only Tab (@ref OnTab) and a dropdown click
     * (@ref OnMouseClick) splice a suggestion into the input.
     */
    void OnEnter();
    /// @brief Delete one code unit before the cursor.
    void OnBackspace();
    /// @brief Delete the word before the cursor.
    void OnBackspaceWord();
    /// @brief Delete one code unit at the cursor.
    void OnDelete();
    /**
     * @brief Splice the highlighted suggestion into the input line.
     *
     * Does not cycle: the selection resets to the top afterwards, so repeated
     * Tab presses re-pick the first item of the recomputed list. Move the
     * selection with Up/Down or the mouse.
     */
    void OnTab();
    /**
     * @brief Move the dropdown selection up, or recall the previous history
     * entry.
     *
     * Suggestions take precedence: history recall applies only when the
     * dropdown has no items (empty input, or no prefix match).
     */
    void OnUp();
    /**
     * @brief Move the dropdown selection down, or recall the next history
     * entry.
     *
     * Same precedence as @ref OnUp - the dropdown wins whenever it has items.
     */
    void OnDown();
    /// @brief Move the cursor left.
    void OnLeft();
    /// @brief Move the cursor right.
    void OnRight();
    /// @brief Move the cursor to the start of the line.
    void OnHome();
    /// @brief Move the cursor to the end of the line.
    void OnEnd();
    /// @brief Close the console. Does not dismiss the dropdown or reset its selection.
    void OnEscape();
    /// @brief Scroll console history by the wheel delta.
    void OnScroll(double yoffset);

    /**
     * @brief Parse and execute a complete command line. Public for testability.
     *
     * Every call first echoes the line into the scrollback, which also pins the
     * view to the bottom, so a blank or whitespace-only line still adds a line.
     * Dispatch is skipped when the line tokenizes to nothing. Unknown verbs
     * print an error.
     */
    void Submit(std::string_view line);

    /**
     * @brief Render the translucent overlay.
     *
     * Caller passes the framebuffer size; this method installs an orthographic
     * projection internally (origin top-left, y increasing downward).
     *
     * Not a pure draw: it is the sole writer of the cached scrollback row count
     * and dropdown rectangle. @ref ScrollToOutputTop and the three mouse
     * handlers read that cache, so they are inert before the first Render and
     * always hit-test the previous frame's layout.
     */
    void Render(IRenderer& renderer, int screenWidth, int screenHeight);

    /**
     * @brief Split @p line on runs of spaces and tabs into views into @p line.
     *
     * Other whitespace (newline, carriage return, vertical tab, form feed) is
     * ordinary token content, so a multi-line string yields a single token.
     *
     * The caller must keep @p line alive for the views' lifetime. Public and
     * static so unit tests can exercise tokenization directly.
     */
    [[nodiscard]] static std::vector<std::string_view> Tokenize(std::string_view line);

    /// @brief Mutable output/input buffer used by command handlers.
    [[nodiscard]] ConsoleBuffer& Buffer() { return m_Buffer; }
    /// @brief Read-only output/input buffer for render and inspection paths.
    [[nodiscard]] const ConsoleBuffer& Buffer() const { return m_Buffer; }
    /// @brief Game instance that owns this console.
    [[nodiscard]] Game& GetGame() { return m_Game; }
    /// @brief Registered command table.
    [[nodiscard]] const ConsoleCommandRegistry& Registry() const { return m_Registry; }

    /**
     * @brief Position the scrollback so the first line of the most recently
     * printed block of @p outputLineCount lines sits at the top of the visible
     * window; the rest fills downward (scroll down to reveal more).
     *
     * Used by `help` so the listing reads from the top instead of pinning to
     * the newest line. Falls back to the bottom before the first Render()
     * (visible rows unknown).
     */
    void ScrollToOutputTop(std::size_t outputLineCount);

    /**
     * @brief Session-scoped player-position bookmarks driven by `bookmark.set` /
     * `bookmark.tp` / `bookmark.list`.
     *
     * Cleared on Console destruction; not persisted to disk.
     */
    [[nodiscard]] std::unordered_map<std::string, glm::ivec2>& Bookmarks() { return m_Bookmarks; }
    [[nodiscard]] const std::unordered_map<std::string, glm::ivec2>& Bookmarks() const
    {
        return m_Bookmarks;
    }

    /**
     * @brief One round of suggestion computation.
     *
     * `items` holds the prefix-matched candidates (alphabetical, capped to the
     * requested count). `wordStart` is the index in the input where the partial
     * word begins, so callers can splice a chosen suggestion in:
     * `input.substr(0, wordStart) + items[i]`. `canonicals` is parallel to
     * `items`: empty entries denote canonical command matches; non-empty entries
     * hold the canonical command that the corresponding alias resolves to.
     * Argument-completion items always have an empty canonical (alias semantics
     * don't apply to arg values).
     */
    struct SuggestionResult
    {
        std::vector<std::string> items;
        std::vector<std::string> canonicals;
        std::size_t wordStart = 0;
    };

    /**
     * @brief Mouse cursor moved over the suggestion dropdown.
     *
     * If the cursor is inside the box, snap @c m_SuggestionIndex to the row
     * under the cursor so hover-to-highlight matches what a click would commit.
     *
     * @param mouseX Cursor x in the pixel space passed to @ref Render.
     * @param mouseY Cursor y in the same space: origin top-left, y downward.
     */
    void OnMouseHover(double mouseX, double mouseY);

    /**
     * @brief Left-click at @p mouseX,mouseY.
     *
     * If the click landed inside the dropdown box, splice the clicked
     * suggestion into the input (same path as Tab) and return true so the
     * caller can swallow the click.
     *
     * @param mouseX Cursor x in the pixel space passed to @ref Render.
     * @param mouseY Cursor y in the same space: origin top-left, y downward.
     * @return       True when the click was consumed by the dropdown.
     */
    bool OnMouseClick(double mouseX, double mouseY);

    /**
     * @brief Mouse wheel hit-routing.
     *
     * Returns true whenever the cursor is inside the last-drawn dropdown,
     * consuming the wheel event; the rows only move when the list overflows the
     * visible window, at 2 rows per notch (the scrollback moves 3, see
     * @ref OnScroll). Returns false otherwise, so the caller scrolls the
     * scrollback instead.
     *
     * @param mouseX  Cursor x in the pixel space passed to @ref Render.
     * @param mouseY  Cursor y in the same space: origin top-left, y downward.
     * @param yoffset Wheel delta in notches; positive reveals earlier rows.
     * @return        True when the wheel event was consumed by the dropdown.
     */
    bool TryScrollDropdown(double mouseX, double mouseY, double yoffset);

private:
    /// @brief Wire the built-in command set. Defined in ConsoleCommands.cpp.
    void RegisterDefaultCommands();

    /**
     * @brief Compute the up-to-@p maxCount autocomplete suggestions for the
     * current input line.
     *
     * Suggests command names while typing the verb, and falls back to the
     * verb's `argCompletions` callback when typing positional arguments. Used
     * by both the dropdown renderer and Tab completion so the visible list and
     * the chosen completion stay in lockstep.
     */
    [[nodiscard]] SuggestionResult ComputeSuggestions(std::size_t maxCount) const;

    /**
     * @brief Slide @c m_SuggestionScroll so @c m_SuggestionIndex stays inside
     * the visible window, then clamp the scroll to a valid range.
     *
     * Called any time the index or item count changes.
     */
    void ClampSuggestionScroll(std::size_t itemCount);

    /**
     * @brief Hard cap on suggestions so a degenerate prefix can't blow up the box.
     *
     * Larger than any realistic command list; raise if completion sources ever
     * return more.
     */
    static constexpr std::size_t kMaxSuggestions = 64;

    /**
     * @brief Maximum rows shown in the dropdown box at once.
     *
     * Items beyond this scroll behind the visible window.
     */
    static constexpr std::size_t kMaxVisibleSuggestions = 8;

    Game& m_Game;
    ConsoleBuffer m_Buffer;
    ConsoleCommandRegistry m_Registry;
    State m_State = State::Closed;
    /**
     * @brief Scrollback row count from the last Render(), cached so
     * ScrollToOutputTop can position the view without re-deriving the overlay
     * layout here.
     */
    int m_LastVisibleLines = 0;
    /**
     * @brief Index of the highlighted entry in the current dropdown (across the
     * full item list, not just the visible window).
     *
     * Reset to 0 on any input modification; clamped to the suggestion count
     * when read.
     */
    std::size_t m_SuggestionIndex = 0;
    /**
     * @brief First visible row in the dropdown's sliding window.
     *
     * Adjusted via arrow-key navigation, mouse wheel, and ClampSuggestionScroll.
     */
    std::size_t m_SuggestionScroll = 0;
    /**
     * @brief Geometry cached during Render() so input handlers (mouse hover,
     * click, wheel) can hit-test the dropdown without their own copy of the
     * layout math.
     *
     * Refreshed every frame; @c visible is false when the dropdown isn't drawn
     * this frame.
     */
    struct DropdownRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float rowH = 0.0f;
        float padTop = 0.0f;
        std::size_t topRow = 0;       ///< First visible item index.
        std::size_t visibleRows = 0;  ///< Rows currently drawn.
        std::size_t totalItems = 0;   ///< Full suggestion count.
        bool visible = false;
    };
    DropdownRect m_LastDropdown;

    /**
     * @brief Session-scoped bookmark storage.
     *
     * Keyed by user-supplied name; value is the player's tile coordinates at
     * the time of `bookmark.set`. Empty by default; not persisted across
     * program runs.
     */
    std::unordered_map<std::string, glm::ivec2> m_Bookmarks;
};

/**
 * @brief Pure `Closed -> Half -> Full -> Closed` rotation over Console::State.
 *
 * Exposed as a free function (and made `constexpr`) so it can be validated
 * without constructing a Console + Game pair, which would require a GL context
 * per the test-suite constraints.
 *
 * @warning This is not the hotkey behavior. No production code calls it - the
 * only callers are tests/ConsoleStateTests.cpp. The real transitions live in
 * @ref Console::Toggle (F12, which only moves between `Closed` and `Half`) and
 * @ref Console::ToggleFullscreen (Tab, which only swaps `Half` and `Full`), so
 * this three-step rotation is reachable from no input path.
 *
 * @param s Current state.
 * @return The next state in the rotation.
 */
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
