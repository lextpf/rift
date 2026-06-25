#include <gtest/gtest.h>

#include "../src/EntityStore.hpp"
#include "../src/Identity.hpp"
#include "../src/NpcRecord.hpp"

#include <ecs.hpp>

#include <cstdint>

// NPCs carry a stable per-session instance id (the Identity component) so
// dialogue / editor / console reference an NPC by identity, not by container
// position. NPCs live in the ECS registry: SpawnNpc assigns a fresh id (or
// preserves an NpcRecord's nonzero id, for undo), and EntityStore::FindById
// resolves it to the entity. Pure data paths (no Game, no GL/Vulkan, no
// WorldServices needed - SpawnNpc tolerates absent services).

TEST(NpcIdentity, InstanceIdsAreUniqueAndNonZero)
{
    ecs::registry world;
    const ecs::entity a = EntityStore::SpawnNpc(world, NpcRecord{});
    const ecs::entity b = EntityStore::SpawnNpc(world, NpcRecord{});
    const ecs::entity c = EntityStore::SpawnNpc(world, NpcRecord{});

    const std::uint64_t ia = world.get<Identity>(a).instanceId;
    const std::uint64_t ib = world.get<Identity>(b).instanceId;
    const std::uint64_t ic = world.get<Identity>(c).instanceId;

    EXPECT_NE(ia, 0u);
    EXPECT_NE(ib, 0u);
    EXPECT_NE(ic, 0u);

    EXPECT_NE(ia, ib);
    EXPECT_NE(ib, ic);
    EXPECT_NE(ia, ic);
}

TEST(NpcIdentity, SnapshotRespawnPreservesId)
{
    // Editor place -> undo -> redo must keep the same identity so a dialogue
    // reference survives. SnapshotNpc captures the instanceId; SpawnNpc reuses a
    // nonzero id rather than minting a new one.
    ecs::registry world;
    const ecs::entity e = EntityStore::SpawnNpc(world, NpcRecord{});
    const std::uint64_t id = world.get<Identity>(e).instanceId;

    const NpcRecord snap = EntityStore::SnapshotNpc(world, e);
    EntityStore::Remove(world, e);

    const ecs::entity respawned = EntityStore::SpawnNpc(world, snap);
    EXPECT_EQ(world.get<Identity>(respawned).instanceId, id);
}

TEST(NpcIdentity, IdSurvivesRegistryRemove)
{
    // Regression for the latent index-shift bug: removing one NPC must not
    // retarget an id-based reference. The registry resolves by identity, so a
    // surviving NPC's id still finds it after another NPC is destroyed.
    ecs::registry world;
    const ecs::entity e0 = EntityStore::SpawnNpc(world, NpcRecord{});
    const ecs::entity e1 = EntityStore::SpawnNpc(world, NpcRecord{});
    EntityStore::SpawnNpc(world, NpcRecord{});

    const std::uint64_t removedId = world.get<Identity>(e0).instanceId;
    const std::uint64_t targetId = world.get<Identity>(e1).instanceId;

    EntityStore::Remove(world, e0);

    EXPECT_EQ(EntityStore::FindById(world, targetId), e1);
    // The removed NPC's id no longer resolves (the ecs::alive == false analog).
    EXPECT_FALSE(EntityStore::FindById(world, removedId));
}
