#include "GameStateManager.h"

#include <string_view>

std::vector<std::string> GameStateManager::GetActiveQuests() const
{
    std::vector<std::string> activeQuests;

    constexpr std::string_view kAcceptedPrefix = "accepted_";

    for (const auto& [key, value] : m_Flags)
    {
        // Check if this is a quest flag that's active
        if (key.find("_quest") != std::string::npos && key.find(kAcceptedPrefix) == 0 &&
            !value.empty() && value != "false" && value != "0")
        {
            // Extract quest name (remove "accepted_" prefix)
            std::string questName = key.substr(kAcceptedPrefix.size());

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

std::string GameStateManager::GetQuestDescription(const std::string& questName) const
{
    std::string flagKey = "accepted_" + questName;
    auto it = m_Flags.find(flagKey);
    if (it != m_Flags.end() && it->second != "true" && it->second != "1" && it->second != "false" &&
        it->second != "0")
    {
        return it->second;
    }
    return "";
}
