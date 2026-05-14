#include "NavigationRecalc.hpp"

#include "Logger.hpp"
#include "NonPlayerCharacter.hpp"
#include "Tilemap.hpp"

#include <algorithm>
#include <utility>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Nav";
}  // namespace

std::vector<NonPlayerCharacter> SnapshotAndEraseNPCsOnNonWalkable(
    const Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs)
{
    std::vector<NonPlayerCharacter> snapshot;
    auto it = std::remove_if(npcs.begin(),
                             npcs.end(),
                             [&](NonPlayerCharacter& npc)
                             {
                                 if (!tilemap.GetNavigation(npc.GetTileX(), npc.GetTileY()))
                                 {
                                     Logger::InfoF(
                                         LOG_SUBSYSTEM,
                                         "Removing NPC at tile ({}, {}) - no longer on navigation",
                                         npc.GetTileX(),
                                         npc.GetTileY());
                                     snapshot.push_back(std::move(npc));
                                     return true;
                                 }
                                 return false;
                             });
    npcs.erase(it, npcs.end());
    return snapshot;
}

void RestoreErasedNPCs(std::vector<NonPlayerCharacter>& npcs,
                       std::vector<NonPlayerCharacter>& snapshot)
{
    for (auto& npc : snapshot)
        npcs.push_back(std::move(npc));
    snapshot.clear();
}

void RebuildPatrolRoutes(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs)
{
    for (auto& npc : npcs)
    {
        if (!npc.ReinitializePatrolRoute(&tilemap))
        {
            Logger::WarnF(LOG_SUBSYSTEM,
                          "NPC at ({}, {}) could not find valid patrol route",
                          npc.GetTileX(),
                          npc.GetTileY());
        }
    }
}
