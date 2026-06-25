#include <gtest/gtest.h>

#include "../src/AnimationState.hpp"
#include "../src/CharacterDirection.hpp"
#include "../src/Elevation.hpp"
#include "../src/Facing.hpp"
#include "../src/NpcAiSystem.hpp"
#include "../src/NpcIdle.hpp"
#include "../src/Patrol.hpp"
#include "../src/PatrolRoute.hpp"
#include "../src/Speed.hpp"
#include "../src/Tilemap.hpp"
#include "../src/Transform.hpp"

#include <glm/glm.hpp>

#include <random>
#include <vector>

// NpcAiSystem's randomness is an explicit std::mt19937& parameter (lifted off a
// former file-local static into the world's globals-owned engine, owned by Game
// and published via WorldServices::npcRng). That makes the idle FSM deterministic
// for a given seed and unit-testable without a Game / GL context. These drive the
// "look around indefinitely" idle branch (no patrol route) purely through data.

namespace
{
// Run `picks` look-around steps from a freshly seeded engine and collect the
// resulting facing sequence. Each Update call lands in the no-route idle branch
// and consumes exactly one direction draw.
std::vector<CharacterDirection> LookAroundSequence(unsigned seed, int picks)
{
    std::mt19937 rng(seed);
    Tilemap tilemap;
    tilemap.SetTilemapSize(10, 10, false);

    Transform xf;
    Elevation elev;
    Facing facing;
    AnimationState anim;
    NpcIdle idle;
    Patrol patrol;
    PatrolRoute route;
    Speed speed;

    // "No path available" idle: stand still and look around on every step.
    idle.standingStill = true;
    idle.randomStandStillTimer = 0.0f;

    std::vector<CharacterDirection> seq;
    seq.reserve(static_cast<std::size_t>(picks));
    for (int i = 0; i < picks; ++i)
    {
        idle.lookAroundTimer = 0.0f;  // force a pick on every call
        NpcAiSystem::Update(
            xf, elev, facing, anim, idle, patrol, route, speed, 0.016f, &tilemap, nullptr, rng);
        seq.push_back(facing.dir);
    }
    return seq;
}
}  // namespace

TEST(NpcAiRng, LookAroundIsDeterministicForSameSeed)
{
    // Same seed -> identical pick sequence: the passed-in engine is the sole
    // source of randomness, with no hidden global state leaking between runs.
    EXPECT_EQ(LookAroundSequence(0xC0FFEEu, 16), LookAroundSequence(0xC0FFEEu, 16));
}

TEST(NpcAiRng, EngineIsActuallyConsumed)
{
    // The sequence varies (not a single constant direction), proving the rng
    // parameter is wired through to the pick rather than ignored. 16 draws from
    // 4 options all colliding on one value has probability (1/4)^15 -- nil.
    const std::vector<CharacterDirection> seq = LookAroundSequence(0x1234u, 16);
    bool allSame = true;
    for (const CharacterDirection dir : seq)
    {
        if (dir != seq.front())
        {
            allSame = false;
            break;
        }
    }
    EXPECT_FALSE(allSame);
}
