#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class GameStateManager
 * @brief Session-scoped store for dialogue and quest flags.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Central storage for all game flags that dialogue conditions can check
 * and consequences can modify. Flags are stored as string key-value pairs.
 *
 * @note Flags live in memory only. Nothing serializes them: the map save written by
 * @c Tilemap::SaveMapToJSON never sees this class, and @c ResetWorldToDefaults clears the
 * store on New Game. Quest progress therefore does not survive "Continue".
 *
 * @note Condition checks are presence-based by default:
 * - `FLAG_SET` means the key exists, regardless of stored value.
 * - `FLAG_NOT_SET` means the key does not exist.
 * Use `FLAG_EQUALS` for value-based checks (e.g., `key == "true"`).
 *
 * @par Flag types
 * |    Type |     Method     | Storage         | Example            |
 * |---------|----------------|-----------------|--------------------|
 * | Boolean |   SetFlag()    | "true" / erased | talked_to_elder    |
 * |  String | SetFlagValue() | Any string      | accepted_ufo_quest |
 *
 * @par Presence vs. value
 * `SetFlag(key, false)` does not store `"false"`: it calls @ref ClearFlag, which
 * erases the key. Nothing in this class ever writes the literal string `"false"` -
 * only @ref SetFlagValue can. The readers then disagree about what counts as "set",
 * because some test presence and others test the stored value. For a quest `"x"`
 * with flag key `k` = `accepted_x`, and no `completed_x` unless a row says so:
 *
 * | Write                      | HasFlag | GetFlag | Active? | Listed? | Description |
 * |----------------------------|---------|---------|---------|---------|-------------|
 * | SetFlag(k)                 | true    | true    | yes     | yes     | "" (!)      |
 * | SetFlag(k, false)          | false   | false   | no      | no      | ""          |
 * | SetFlagValue(k, "d")       | true    | false   | yes     | yes     | "d"         |
 * | SetFlagValue(k, "false")   | true    | false   | yes     | no  (!) | ""          |
 * | AcceptQuest("x", "d")      | true    | false   | yes     | yes     | "d"         |
 * | ...then CompleteQuest("x") | true    | false   | no      | no      | "d"         |
 *
 * Columns are `HasFlag(k)`, `GetFlag(k)`, `IsQuestActive("x")`, whether `"x"` appears
 * in `GetActiveQuests()`, and `GetQuestDescription("x")`.
 *
 * The two `(!)` cells are the traps:
 * - Row 1: a quest accepted with plain @ref SetFlag stores `"true"`, and
 *   @ref GetQuestDescription treats `"true"`/`"false"` as "no description", so the
 *   journal text comes back empty. Use @ref AcceptQuest or @ref SetFlagValue.
 * - Row 4: @ref IsQuestActive only asks whether the key exists, while
 *   @ref GetActiveQuests additionally rejects values that are empty, `"false"` or
 *   `"0"`. The same flag is "active" to one API and not to the other.
 *
 * The completion side splits the same way: @ref IsQuestActive treats any
 * `completed_x` key as completed, while @ref GetActiveQuests only counts it completed
 * when the value is exactly `"true"`. So `SetFlagValue("completed_x", "pending")`
 * makes `IsQuestActive("x")` false while `GetActiveQuests()` still lists `"x"`.
 *
 * @par Quest lifecycle
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     classDef available fill:#164e54,stroke:#06b6d4,color:#e2e8f0
 *     classDef active fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *     classDef completed fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *
 *     state "Quest Available" as Available:::available
 *     state "Quest Active" as Active:::active
 *     state "Quest Completed" as Completed:::completed
 *
 *     [*] --> Available
 *     Available --> Active: Player accepts
 *     Active --> Completed: Objective done
 *     Completed --> [*]
 *
 *     note right of Available: FLAG_NOT_SET accepted_X_quest
 *     note right of Active: FLAG_SET accepted_X_quest
 *     note right of Completed: FLAG_SET completed_X_quest
 * </pre>
 * @endhtmlonly
 *
 * @par Condition evaluation flow
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef good fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef bad fill:#4a2020,stroke:#ef4444,color:#e2e8f0
 *
 *     A[DialogueOption]:::core --> B{Has conditions?}
 *     B -->|No| C[Show option]:::good
 *     B -->|Yes| D{All conditions pass?}
 *     D -->|Yes| C
 *     D -->|No| E[Hide option]:::bad
 * </pre>
 * @endhtmlonly
 *
 * @par Condition types
 * |         Type |     Check      | Use Case                    |
 * |--------------|----------------|-----------------------------|
 * |     FLAG_SET |   HasFlag()    | Show if quest accepted      |
 * | FLAG_NOT_SET |  !HasFlag()    | Show if quest not yet taken |
 * |  FLAG_EQUALS | GetFlagValue() | Check specific state        |
 *
 * @par Quest flag naming
 * @verbatim
 * accepted_<name>_quest  -> "Quest description here"
 * completed_<name>_quest -> "true"
 * @endverbatim
 *
 * @par Example: UFO quest
 * @code{.cpp}
 * // Accept quest with description
 * stateManager.SetFlagValue("accepted_ufo_quest", "Find Anna's brother!");
 *
 * // Check if quest active
 * if (stateManager.HasFlag("accepted_ufo_quest") &&
 *     !stateManager.HasFlag("completed_ufo_quest")) {
 *     // Quest is in progress
 * }
 *
 * // Complete quest
 * stateManager.SetFlag("completed_ufo_quest", true);
 * @endcode
 */
class GameStateManager
{
public:
    GameStateManager() = default;

    /**
     * @brief Set a boolean flag: store `"true"`, or erase the key entirely.
     *
     * There is no stored `false`. @p value == false delegates to @ref ClearFlag, so
     * the key disappears and @ref HasFlag reports it as unset. See "Presence vs. value" above
     * for the reader-by-reader consequences.
     *
     * @param key Flag name.
     * @param value True stores `"true"`; false erases the key.
     */
    void SetFlag(const std::string& key, bool value = true)
    {
        if (value)
            m_Flags[key] = "true";
        else
            ClearFlag(key);
    }

    /**
     * @brief Get a boolean flag value.
     *
     * Value-based, so a key written with @ref SetFlagValue (a quest description, a
     * visit counter, ...) reads back as false here even though @ref HasFlag is true.
     *
     * @param key Flag name.
     * @return True if flag is set and equals "true", false otherwise.
     */
    [[nodiscard]] bool GetFlag(const std::string& key) const
    {
        auto it = m_Flags.find(key);
        if (it == m_Flags.end())
        {
            return false;
        }
        return it->second == "true";
    }

    /**
     * @brief Clear/unset a flag by removing its key.
     * @param key Flag name.
     */
    void ClearFlag(const std::string& key) { m_Flags.erase(key); }

    /**
     * @brief Set a flag to a string value.
     * @param key Flag name.
     * @param value String value.
     */
    void SetFlagValue(const std::string& key, const std::string& value) { m_Flags[key] = value; }

    /**
     * @brief Get a flag's string value.
     * @param key Flag name.
     * @return The value, or empty string if not set.
     */
    [[nodiscard]] std::string GetFlagValue(const std::string& key) const
    {
        auto it = m_Flags.find(key);
        return (it != m_Flags.end()) ? it->second : "";
    }

    /**
     * @brief Check if a flag exists, whatever its value.
     *
     * The stored value can be the string `"false"` and this still returns true - but
     * only @ref SetFlagValue can create that state, since @ref SetFlag erases instead
     * of storing `"false"`.
     *
     * @param key Flag name.
     * @return True if the flag key exists.
     */
    [[nodiscard]] bool HasFlag(const std::string& key) const
    {
        return m_Flags.find(key) != m_Flags.end();
    }

    /// @brief Clear all state.
    void Clear() { m_Flags.clear(); }

    /**
     * @name Typed quest API
     * @brief Structured quest operations that enforce the naming convention.
     *
     * These methods use the same underlying flag storage as SetFlag/HasFlag
     * but encode the `accepted_`/`completed_` prefix convention so callers
     * cannot mistype flag names.
     */
    /// @{

    /**
     * @brief Accept a quest and store its description.
     *
     * Sets `"accepted_<questName>"` to the given description string.
     *
     * @param questName Quest identifier (e.g., "ufo_quest").
     * @param description Human-readable quest objective text.
     */
    void AcceptQuest(const std::string& questName, const std::string& description)
    {
        m_Flags["accepted_" + questName] = description;
    }

    /**
     * @brief Mark a quest as completed.
     *
     * Sets `"completed_<questName>"` to `"true"`.
     *
     * @param questName Quest identifier (e.g., "ufo_quest").
     */
    void CompleteQuest(const std::string& questName) { m_Flags["completed_" + questName] = "true"; }

    /**
     * @brief Check if a quest is currently active (accepted but not completed).
     *
     * Presence-based on both keys: any `accepted_<name>` counts as accepted whatever
     * its value, and any `completed_<name>` counts as completed whatever its value.
     * @ref GetActiveQuests answers the same question by value and can disagree; see
     * "Presence vs. value" in the class documentation.
     *
     * @param questName Quest identifier (e.g., "ufo_quest").
     * @return True if accepted and not yet completed.
     */
    [[nodiscard]] bool IsQuestActive(const std::string& questName) const
    {
        return HasFlag("accepted_" + questName) && !HasFlag("completed_" + questName);
    }

    /**
     * @brief Check if a quest has been completed.
     * @param questName Quest identifier (e.g., "ufo_quest").
     * @return True if the completed flag exists.
     */
    [[nodiscard]] bool IsQuestCompleted(const std::string& questName) const
    {
        return HasFlag("completed_" + questName);
    }

    /// @}

    /**
     * @brief Get list of active quest names.
     *
     * Value-based, unlike @ref IsQuestActive, which is presence-based. An
     * `accepted_*` key is skipped when its value is empty, `"false"` or `"0"`, and a
     * quest counts as completed only when `completed_<name>` is exactly `"true"`.
     * The two APIs therefore disagree on both halves of that test; see
     * "Presence vs. value" in the class documentation.
     *
     * @return Vector of quest names (without "accepted_" prefix), in unspecified
     *         order (it walks an unordered_map).
     */
    [[nodiscard]] std::vector<std::string> GetActiveQuests() const;

    /**
     * @brief Get a quest's description from its `accepted_<name>` flag value.
     *
     * @note The values `"true"` and `"false"` are treated as "no description" and
     * yield `""`. A quest accepted with `SetFlag("accepted_x")` stores `"true"` and
     * therefore has no description; store the text with @ref AcceptQuest or
     * @ref SetFlagValue instead.
     *
     * @param questName Quest identifier (e.g., "ufo_quest").
     * @return The quest description; `""` when unset, or when the stored value is
     *         `"true"` or `"false"`.
     */
    [[nodiscard]] std::string GetQuestDescription(const std::string& questName) const;

    /**
     * @brief Get read-only view of every stored flag.
     *
     * Exposed primarily for the developer console's `flag.list` command so it
     * can iterate flags without mutating storage. The returned reference is
     * stable until the next call that mutates `m_Flags`.
     *
     * @return Const reference to the underlying key/value flag map.
     */
    [[nodiscard]] const std::unordered_map<std::string, std::string>& GetAllFlags() const
    {
        return m_Flags;
    }

    GameStateManager(const GameStateManager&) = default;
    GameStateManager& operator=(const GameStateManager&) = default;
    GameStateManager(GameStateManager&&) = default;
    GameStateManager& operator=(GameStateManager&&) = default;

private:
    std::unordered_map<std::string, std::string> m_Flags;  ///< All flags as strings.
};
