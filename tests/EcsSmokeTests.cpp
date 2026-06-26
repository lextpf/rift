#include "AnimationState.hpp"
#include "Appearance.hpp"
#include "Dialogue.hpp"
#include "DialogueHandle.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "Hitbox.hpp"
#include "Identity.hpp"
#include "Motor.hpp"
#include "MotorParams.hpp"
#include "NpcIdle.hpp"
#include "NpcSprite.hpp"
#include "NpcTag.hpp"
#include "Patrol.hpp"
#include "PatrolRoute.hpp"
#include "PlayerInputState.hpp"
#include "PlayerModes.hpp"
#include "PlayerMovementState.hpp"
#include "PlayerSprite.hpp"
#include "PlayerTag.hpp"
#include "Speed.hpp"
#include "TextureHandle.hpp"
#include "Transform.hpp"

#include <gtest/gtest.h>

#include <ecs.hpp>

#include <type_traits>

// Phase 0 canary for the ECS integration: proves the bundled single-header ecs
// library (external/ecs/ecs.hpp) compiles and links under Rift's MSVC
// /std:c++latest /Zc:__cplusplus /EHa toolchain, exercising the core registry
// API the integration plan relies on. Pure data paths - no GL/Vulkan context is
// created (honoring the rift_tests constraint in CMakeLists.txt).

// Round 1 component-eligibility gate: every character component extracted from
// the GameCharacter/PlayerCharacter classes must be a flat aggregate the ECS can
// store in packed, reflectable form - no user constructors, base classes, or
// virtuals, and at most 16 fields (the tie_fields cap). If a future edit
// reintroduces a constructor or inheritance, these fail at compile time.
namespace
{
template <class T>
inline constexpr bool ecs_component_ready =
    ecs::reflectable_aggregate<T> && ecs::field_count_v<T> <= 16;
}  // namespace

static_assert(ecs_component_ready<Transform>);
static_assert(ecs_component_ready<Elevation>);
static_assert(ecs_component_ready<Facing>);
static_assert(ecs_component_ready<AnimationState>);
static_assert(ecs_component_ready<Identity>);
static_assert(ecs_component_ready<MotorParams>);
static_assert(ecs_component_ready<Motor>);
static_assert(ecs_component_ready<PlayerMovementState>);
static_assert(ecs_component_ready<NpcIdle>);
static_assert(ecs_component_ready<Patrol>);
static_assert(ecs_component_ready<PlayerModes>);
static_assert(ecs_component_ready<PlayerInputState>);
static_assert(ecs_component_ready<Appearance>);
// Dialogue carries the 3 reflectable strings + a DialogueHandle into the
// DialogueStore (the non-reflectable tree graph lives there, not in the component).
static_assert(ecs_component_ready<Dialogue>);
static_assert(ecs_component_ready<DialogueHandle>);
// Archetype tags: empty structs the ECS stores as zero-byte tags, used to select
// player vs NPC entities in shared view/each queries.
static_assert(std::is_empty_v<PlayerTag>);
static_assert(std::is_empty_v<NpcTag>);
static_assert(ecs_component_ready<Speed>);
// Hitbox: the player's collision-box config (replaces the former CollisionResolver
// class's immutable m_Hitbox); the collision pipeline is now CollisionSystem free
// functions that take a const Hitbox&.
static_assert(ecs_component_ready<Hitbox>);
// Sprite components carry a runtime const Texture* atlas cache (not persisted),
// like Identity is runtime-only; structurally they are still flat aggregates.
static_assert(ecs_component_ready<NpcSprite>);
static_assert(ecs_component_ready<PlayerSprite>);
// TextureHandle is the field a future Sprite component carries instead of an
// owned Texture; it too must stay a flat aggregate.
static_assert(ecs_component_ready<TextureHandle>);

// PatrolRoute is a *non-reflectable* component (it owns a std::vector of
// waypoints and is regenerated, never serialized), so it is NOT an
// ecs_component_ready aggregate. It is still a valid packed component: the packed
// pool requires only move-constructibility + swappability, which these lock so a
// future change that breaks packed storage fails here at compile time.
static_assert(std::is_move_constructible_v<PatrolRoute>);
static_assert(std::is_swappable_v<PatrolRoute>);

namespace
{
struct Position
{
    float x;
    float y;
};

struct Velocity
{
    float dx;
    float dy;
};

struct FrameClock
{
    float hours;
};
}  // namespace

TEST(EcsSmoke, CreateGetHasDestroy)
{
    ecs::registry world;

    const ecs::entity e = world.create(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});
    EXPECT_TRUE(world.alive(e));
    EXPECT_TRUE(world.has<Position>(e));
    EXPECT_TRUE(world.has<Velocity>(e));

    EXPECT_FLOAT_EQ(world.get<Position>(e).x, 1.0f);
    EXPECT_FLOAT_EQ(world.get<Velocity>(e).dx, 3.0f);

    world.destroy(e);
    EXPECT_FALSE(world.alive(e));
}

TEST(EcsSmoke, EachAppliesVelocity)
{
    ecs::registry world;
    const ecs::entity e = world.create(Position{0.0f, 0.0f}, Velocity{5.0f, -2.0f});

    world.each<Position, const Velocity>(
        [](Position& p, const Velocity& v)
        {
            p.x += v.dx;
            p.y += v.dy;
        });

    EXPECT_FLOAT_EQ(world.get<Position>(e).x, 5.0f);
    EXPECT_FLOAT_EQ(world.get<Position>(e).y, -2.0f);
}

TEST(EcsSmoke, ViewCountAndFind)
{
    ecs::registry world;
    world.create(Position{0.0f, 0.0f});
    world.create(Position{1.0f, 1.0f}, Velocity{1.0f, 1.0f});

    // Inner-join view: only the entity carrying both components matches.
    auto moving = world.view<Position, const Velocity>();
    EXPECT_EQ(moving.count(), static_cast<std::size_t>(1));

    const ecs::entity stationary = world.create(Position{9.0f, 9.0f});
    EXPECT_NE(world.find<Position>(stationary), nullptr);
    EXPECT_EQ(world.find<Velocity>(stationary), nullptr);
}

TEST(EcsSmoke, GlobalsSingleton)
{
    // The lazy globals entity backs engine singletons (e.g. TimeOfDay in Phase 1).
    ecs::registry world;
    world.globals().obtain<FrameClock>().hours = 12.0f;
    EXPECT_FLOAT_EQ(world.globals().obtain<FrameClock>().hours, 12.0f);
}
