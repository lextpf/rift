#include "GameStateManager.h"

#include <string_view>

std::vector<std::string> GameStateManager::GetActiveQuests() const
{
    std::vector<std::string> activeQuests;

    constexpr std::string_view kAcceptedPrefix = "accepted_";

    for (const auto& [key, value] : m_Flags)
    {
        // Match keys that start with "accepted_" and end with "_quest".
        // Using ends_with() avoids false-matching keys like "accepted_quest_status".
        if (key.starts_with(kAcceptedPrefix) && key.ends_with("_quest") && !value.empty() &&
            value != "false" && value != "0")
        {
            // Extract quest name (remove "accepted_" prefix)
            std::string questName = key.substr(kAcceptedPrefix.size());

            // Check if this quest is not completed
            std::string completedKey = "completed_" + questName;
            auto completedIt = m_Flags.find(completedKey);
            if (completedIt == m_Flags.end() || completedIt->second != "true")
            {
                activeQuests.push_back(questName);
            }
        }
    }

    return activeQuests;
}

std::string GameStateManager::GetQuestDescription(const std::string& questName) const
{
    std::string flagKey = "accepted_" + questName;
    auto it = m_Flags.find(flagKey);
    if (it != m_Flags.end() && it->second != "true" && it->second != "false")
    {
        return it->second;
    }
    return "";
}
