#include "NavigationRecalc.h"

#include "NonPlayerCharacter.h"
#include "Tilemap.h"

#include <algorithm>
#include <iostream>
#include <utility>

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
                                     std::cout << "Removing NPC at tile (" << npc.GetTileX() << ", "
                                               << npc.GetTileY() << ") - no longer on navigation"
                                               << std::endl;
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
            std::cout << "Warning: NPC at (" << npc.GetTileX() << ", " << npc.GetTileY()
                      << ") could not find valid patrol route" << std::endl;
        }
    }
}
