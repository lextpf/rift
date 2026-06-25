#include <gtest/gtest.h>

#include "../src/AnimationState.hpp"
#include "../src/Dialogue.hpp"
#include "../src/Elevation.hpp"
#include "../src/EntityStore.hpp"
#include "../src/Facing.hpp"
#include "../src/Identity.hpp"
#include "../src/NpcIdle.hpp"
#include "../src/NpcRecord.hpp"
#include "../src/NpcSprite.hpp"
#include "../src/NpcTag.hpp"
#include "../src/Patrol.hpp"
#include "../src/PatrolRoute.hpp"
#include "../src/Speed.hpp"
#include "../src/Transform.hpp"

#include <ecs.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// EntityStore is the NPC lifecycle + query seam over the registry: spawn
// (SpawnNpc -> entity from an NpcRecord blueprint), despawn (Remove / Clear),
// count, the index/id lookups the console + editor need (Entities / FindById),
// snapshot (for undo), and BuildNpcFeet. NPCs are granular components tagged
// NpcTag. These tests run WITHOUT WorldServices published in globals: SpawnNpc
// tolerates absent services (the sprite stays an invalid handle), so the data
// paths still exercise without a renderer.

TEST(EntityStore, SpawnCreatesEntities)
{
    ecs::registry world;
    EntityStore::SpawnNpc(world, NpcRecord{});
    EntityStore::SpawnNpc(world, NpcRecord{});
    EXPECT_EQ(EntityStore::Count(world), static_cast<std::size_t>(2));
}

TEST(EntityStore, SpawnAttachesFullComponentSet)
{
    // R4 guard: an NPC missing any component silently drops out of the system
    // each<...> views. SpawnNpc must attach the complete tuple + NpcTag.
    ecs::registry world;
    const ecs::entity e = EntityStore::SpawnNpc(world, NpcRecord{});
    ASSERT_TRUE(world.alive(e));
    EXPECT_TRUE((world.has_all<Transform,
                               Elevation,
                               Facing,
                               AnimationState,
                               Speed,
                               Identity,
                               NpcSprite,
                               Dialogue,
                               NpcIdle,
                               Patrol,
                               PatrolRoute>(e)));
    EXPECT_TRUE(world.has<NpcTag>(e));
}

TEST(EntityStore, SpawnPositionsAtTileAndAssignsFreshIdentity)
{
    ecs::registry world;
    NpcRecord rec;
    rec.tileX = 3;
    rec.tileY = 4;
    rec.tileSize = 16;
    const ecs::entity e = EntityStore::SpawnNpc(world, rec);
    EXPECT_EQ(world.get<Patrol>(e).tileX, 3);
    EXPECT_EQ(world.get<Patrol>(e).tileY, 4);
    // Feet at bottom-center of the tile.
    EXPECT_FLOAT_EQ(world.get<Transform>(e).position.x, 3 * 16 + 8.0f);
    EXPECT_FLOAT_EQ(world.get<Transform>(e).position.y, 4 * 16 + 16.0f);
    EXPECT_NE(world.get<Identity>(e).instanceId, 0u);
}

TEST(EntityStore, SpawnPreservesNonzeroInstanceId)
{
    ecs::registry world;
    NpcRecord rec;
    rec.instanceId = 4242;  // undo/redo round-trips a nonzero id
    const ecs::entity e = EntityStore::SpawnNpc(world, rec);
    EXPECT_EQ(world.get<Identity>(e).instanceId, static_cast<std::uint64_t>(4242));
}

TEST(EntityStore, RemoveDestroysOneKeepsOthers)
{
    ecs::registry world;
    EntityStore::SpawnNpc(world, NpcRecord{});
    const ecs::entity mid = EntityStore::SpawnNpc(world, NpcRecord{});
    const ecs::entity last = EntityStore::SpawnNpc(world, NpcRecord{});
    const std::uint64_t keepId = world.get<Identity>(last).instanceId;

    EntityStore::Remove(world, mid);

    EXPECT_EQ(EntityStore::Count(world), static_cast<std::size_t>(2));
    EXPECT_FALSE(world.alive(mid));
    // The survivor kept its identity and is still resolvable through the seam.
    EXPECT_TRUE(EntityStore::FindById(world, keepId));
}

TEST(EntityStore, RemoveDeadEntityIsNoOp)
{
    ecs::registry world;
    const ecs::entity e = EntityStore::SpawnNpc(world, NpcRecord{});
    EntityStore::Remove(world, e);
    EntityStore::Remove(world, e);  // second remove must not crash
    EXPECT_EQ(EntityStore::Count(world), static_cast<std::size_t>(0));
}

TEST(EntityStore, ClearRemovesAll)
{
    ecs::registry world;
    EntityStore::SpawnNpc(world, NpcRecord{});
    EntityStore::SpawnNpc(world, NpcRecord{});
    EntityStore::Clear(world);
    EXPECT_EQ(EntityStore::Count(world), static_cast<std::size_t>(0));
}

TEST(EntityStore, EntitiesReturnsAllAlive)
{
    ecs::registry world;
    EntityStore::SpawnNpc(world, NpcRecord{});
    EntityStore::SpawnNpc(world, NpcRecord{});
    const std::vector<ecs::entity> all = EntityStore::Entities(world);
    ASSERT_EQ(all.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(world.alive(all[0]));
    EXPECT_TRUE(world.alive(all[1]));
}

TEST(EntityStore, FindByIdResolvesAndMisses)
{
    ecs::registry world;
    const ecs::entity e = EntityStore::SpawnNpc(world, NpcRecord{});
    const std::uint64_t id = world.get<Identity>(e).instanceId;

    EXPECT_EQ(EntityStore::FindById(world, id), e);
    EXPECT_FALSE(EntityStore::FindById(world, id + 99999u));  // unknown id
    EXPECT_FALSE(EntityStore::FindById(world, 0u));           // id 0 never resolves
}

TEST(EntityStore, SnapshotRoundTripsAuthoredState)
{
    ecs::registry world;
    NpcRecord rec;
    rec.type = "guard";
    rec.name = "Bob";
    rec.text = "Halt!";
    rec.tileX = 5;
    rec.tileY = 6;
    rec.instanceId = 77;
    const ecs::entity e = EntityStore::SpawnNpc(world, rec);

    const NpcRecord snap = EntityStore::SnapshotNpc(world, e);
    EXPECT_EQ(snap.type, "guard");
    EXPECT_EQ(snap.name, "Bob");
    EXPECT_EQ(snap.text, "Halt!");
    EXPECT_EQ(snap.tileX, 5);
    EXPECT_EQ(snap.tileY, 6);
    EXPECT_EQ(snap.instanceId, static_cast<std::uint64_t>(77));
}

TEST(BuildNpcFeet, CollectsFeetPositions)
{
    ecs::registry world;
    const ecs::entity a = EntityStore::SpawnNpc(world, NpcRecord{});
    const ecs::entity b = EntityStore::SpawnNpc(world, NpcRecord{});
    world.get<Transform>(a).position = glm::vec2(10.0f, 20.0f);
    world.get<Transform>(b).position = glm::vec2(-5.0f, 7.5f);

    std::vector<glm::vec2> feet;
    BuildNpcFeet(world, feet);

    ASSERT_EQ(feet.size(), static_cast<std::size_t>(2));
    // Order is the registry's dense iteration order; assert both are present
    // without depending on which comes first.
    bool has10 = false;
    bool hasNeg5 = false;
    for (const glm::vec2& f : feet)
    {
        if (f.x == 10.0f && f.y == 20.0f)
        {
            has10 = true;
        }
        if (f.x == -5.0f && f.y == 7.5f)
        {
            hasNeg5 = true;
        }
    }
    EXPECT_TRUE(has10);
    EXPECT_TRUE(hasNeg5);
}

TEST(BuildNpcFeet, ClearsStaleOutputBeforeFilling)
{
    ecs::registry world;
    EntityStore::SpawnNpc(world, NpcRecord{});

    std::vector<glm::vec2> feet{glm::vec2(99.0f), glm::vec2(99.0f), glm::vec2(99.0f)};
    BuildNpcFeet(world, feet);

    EXPECT_EQ(feet.size(), static_cast<std::size_t>(1));
}
