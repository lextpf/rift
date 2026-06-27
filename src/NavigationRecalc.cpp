#include "NavigationRecalc.hpp"

#include "EntityStore.hpp"
#include "Logger.hpp"
#include "NpcAiSystem.hpp"
#include "NpcIdle.hpp"
#include "NpcRecord.hpp"
#include "NpcTag.hpp"
#include "Patrol.hpp"
#include "PatrolRoute.hpp"
#include "Tilemap.hpp"
#include "WorldServices.hpp"

#include <random>
#include <utility>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Nav";
}  // namespace

std::vector<NpcRecord> SnapshotAndEraseNPCsOnNonWalkable(const Tilemap& tilemap,
                                                         ecs::registry& npcs)
{
    // Collect the displaced entities first; destroying components while iterating
    // the same pool is a fault.
    std::vector<ecs::entity> doomed;
    npcs.each<const Patrol, const NpcTag>(
        [&](ecs::entity e, const Patrol& patrol)
        {
            if (!tilemap.GetNavigation(patrol.tileX, patrol.tileY))
            {
                Logger::InfoF(LOG_SUBSYSTEM,
                              "Removing NPC at tile ({}, {}) - no longer on navigation",
                              patrol.tileX,
                              patrol.tileY);
                doomed.push_back(e);
            }
        });

    std::vector<NpcRecord> snapshot;
    snapshot.reserve(doomed.size());
    for (const ecs::entity e : doomed)
    {
        snapshot.push_back(EntityStore::SnapshotNpc(npcs, e));
        EntityStore::Remove(npcs, e);
    }
    return snapshot;
}

void RestoreErasedNPCs(ecs::registry& npcs, std::vector<NpcRecord>& snapshot)
{
    for (const NpcRecord& rec : snapshot)
        EntityStore::SpawnNpc(npcs, rec);
    snapshot.clear();
}

void RebuildPatrolRoutes(Tilemap& tilemap, ecs::registry& npcs)
{
    // Reach the world's RNG (owned by Game, published in globals). The editor
    // recalc paths only have the registry, not Game; pulling from globals keeps
    // the command Apply/Revert signatures free of an RNG param. A local fallback
    // covers callers without published services (e.g. tests).
    const WorldServices* svc = npcs.globals().find<WorldServices>();
    std::mt19937 fallback;
    std::mt19937& rng = (svc != nullptr && svc->npcRng != nullptr) ? *svc->npcRng : fallback;

    npcs.each<NpcIdle, Patrol, PatrolRoute, NpcTag>(
        [&](NpcIdle& idle, Patrol& patrol, PatrolRoute& route)
        {
            if (!NpcAiSystem::ReinitializePatrolRoute(idle, patrol, route, &tilemap, rng))
            {
                Logger::WarnF(LOG_SUBSYSTEM,
                              "NPC at ({}, {}) could not find valid patrol route",
                              patrol.tileX,
                              patrol.tileY);
            }
        });
}
