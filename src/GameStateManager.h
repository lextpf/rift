#pragma once

#include "DialogueSystem.h"

#include <string>
#include <unordered_map>

/**
 * @class GameStateManager
 * @brief Manages persistent game state flags and variables.
 * @author Alex (https://github.com/lextpf)
 *
 * Central storage for all game flags that dialogue conditions can check
 * and consequences can
 * modify. Flags are stored as string key-value pairs.
 *
 * @note Condition checks are
 * presence-based by default:
 * - `FLAG_SET` means the key exists, regardless of stored value.
 * -
 * `FLAG_NOT_SET` means the key does not exist.
 * Use `FLAG_EQUALS` for value-based checks (e.g.,
 * `key == "true"`).
 *
 * @section flag_types Flag Types
 * |    Type |     Method     | Storage    | Example            |
 * |---------|----------------|------------|--------------------|
 * | Boolean |   SetFlag()    | true/false | talked_to_elder    |
 * |  String | SetFlagValue() | Any string | accepted_ufo_quest |
 *
 * @section quest_lifecycle Quest Lifecycle
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
 * @section condition_flow Condition Evaluation Flow
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
 * @section condition_types Condition Types
 * |         Type |     Check      | Use Case                    |
 * |--------------|----------------|-----------------------------|
 * |     FLAG_SET |   HasFlag()    | Show if quest accepted      |
 * | FLAG_NOT_SET |  !HasFlag()    | Show if quest not yet taken |
 * |  FLAG_EQUALS | GetFlagValue() | Check specific state        |
 *
 * @section quest_flags Quest Flag Naming
 * ```
 * accepted_<name>_quest  -> "Quest description here"
 * completed_<name>_quest -> "true"
 * ```
 *
 * @par Example: UFO Quest
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
     * @brief Set a boolean flag.
     * @param key Flag name.
     * @param value True or false.
     */
    void SetFlag(const std::string& key, bool value = true)
    {
        m_Flags[key] = value ? "true" : "false";
    }

    /**
     * @brief Get a boolean flag value.
     * @param key Flag name.
     * @return True if flag is set and equals true or 1, false otherwise.
     */
    [[nodiscard]] bool GetFlag(const std::string& key) const
    {
        auto it = m_Flags.find(key);
        if (it == m_Flags.end())
            return false;
        return (it->second == "true" || it->second == "1");
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
     * @brief Check if a flag exists.
     * @param key Flag name.
     * @return True if
     * the flag key exists (value may be "false").
     */
    [[nodiscard]] bool HasFlag(const std::string& key) const
    {
        return m_Flags.find(key) != m_Flags.end();
    }

    /**
     * @brief Evaluate a single condition.
     * @param condition The condition to check.
     * @return True if condition is met.
     */
    [[nodiscard]] bool EvaluateCondition(const DialogueCondition& condition) const
    {
        switch (condition.type)
        {
            case DialogueCondition::Type::FLAG_SET:
                return HasFlag(condition.key);

            case DialogueCondition::Type::FLAG_NOT_SET:
                return !HasFlag(condition.key);

            case DialogueCondition::Type::FLAG_EQUALS:
                return GetFlagValue(condition.key) == condition.value;

            default:
                return true;
        }
    }

    /**
     * @brief Evaluate multiple conditions (&& logic).
     * @param conditions Vector of conditions.
     * @return True if all conditions are met.
     */
    [[nodiscard]] bool EvaluateConditions(const std::vector<DialogueCondition>& conditions) const
    {
        for (const auto& condition : conditions)
        {
            if (!EvaluateCondition(condition))
            {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Clear all state.
     */
    void Clear() { m_Flags.clear(); }

    /**
     * @brief Get list of active quest names.
     * @return Vector of quest names (without "accepted_" prefix).
     */
    [[nodiscard]] std::vector<std::string> GetActiveQuests() const
    {
        std::vector<std::string> activeQuests;

        for (const auto& [key, value] : m_Flags)
        {
            // Check if this is a quest flag that's active
            if (key.find("_quest") != std::string::npos && key.find("accepted_") == 0 &&
                !value.empty() && value != "false" && value != "0")
            {
                // Extract quest name
                std::string questName = key.substr(9);  // Remove prefix

                // Check if this quest is not completed
                std::string completedKey = "completed_" + questName;
                auto completedIt = m_Flags.find(completedKey);
                if (completedIt == m_Flags.end() ||
                    (completedIt->second != "true" && completedIt->second != "1"))
                {
                    activeQuests.push_back(questName);
                }
            }
        }

        return activeQuests;
    }

    /**
     * @brief Get a quest's description from its flag value.
     * @param questName Quest identifier (e.g., "ufo_quest")
     * @return The quest description, or empty string if not set
     */
    [[nodiscard]] std::string GetQuestDescription(const std::string& questName) const
    {
        std::string flagKey = "accepted_" + questName;
        auto it = m_Flags.find(flagKey);
        if (it != m_Flags.end() && it->second != "true" && it->second != "1" &&
            it->second != "false" && it->second != "0")
        {
            return it->second;
        }
        return "";
    }

private:
    std::unordered_map<std::string, std::string> m_Flags;  ///< All flags as strings
};
