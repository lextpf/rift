#include "GameStateManager.hpp"

#include <string_view>

std::vector<std::string> GameStateManager::GetActiveQuests() const
{
    std::vector<std::string> activeQuests;

    constexpr std::string_view kAcceptedPrefix = "accepted_";

    for (const auto& [key, value] : m_Flags)
    {
        // Value-based acceptance test, unlike IsQuestActive, which only asks whether
        // the key exists. An accepted_* flag whose value is empty, "false" or "0" is
        // treated as not accepted here but is "active" to IsQuestActive.
        if (key.starts_with(kAcceptedPrefix) && !value.empty() && value != "false" && value != "0")
        {
            // Extract quest name (remove "accepted_" prefix)
            std::string questName = key.substr(kAcceptedPrefix.size());

            // Same asymmetry on the completion side: only the exact string "true"
            // completes a quest here, whereas IsQuestActive treats the mere presence
            // of completed_<name> as completion.
            std::string completedKey = "completed_" + questName;
            auto completedIt = m_Flags.find(completedKey);
            if (completedIt == m_Flags.end() || completedIt->second != "true")
            {
                activeQuests.push_back(questName);
            }
        }
    }

    // Iteration order follows the unordered_map, so the result order is unspecified.

    return activeQuests;
}

std::string GameStateManager::GetQuestDescription(const std::string& questName) const
{
    std::string flagKey = "accepted_" + questName;
    // "true"/"false" are the boolean spellings SetFlag/SetFlagValue produce, not a
    // journal line, so they are reported as "no description". Consequence: a quest
    // accepted via SetFlag("accepted_x") - which stores "true" - silently has no
    // description; AcceptQuest / SetFlagValue must be used to attach one.
    auto it = m_Flags.find(flagKey);
    if (it != m_Flags.end() && it->second != "true" && it->second != "false")
    {
        return it->second;
    }
    return "";
}
