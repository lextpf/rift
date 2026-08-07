// Pure-logic guard for the per-tile stance policy. No GL/Vulkan context is
// created here (see the rift_tests constraint in CMakeLists.txt).
//
// This rule decides the shape of every authored map in 3D, so it is worth
// pinning explicitly - and worth pinning what it deliberately does NOT consider.
#include "../src/TileRole.hpp"

#include <gtest/gtest.h>

TEST(TileRoleTest, PlainTilesLieFlat)
{
    // Grass, paths, water: the floor of the world.
    EXPECT_FALSE(tileRole::IsUpright(TileStance::Flat));
}

TEST(TileRoleTest, EveryNonFlatStanceStandsUp)
{
    // Uprightness is the whole meaning of "not Flat". A stance that stood a tile
    // up only sometimes would put the decision back into inference.
    EXPECT_TRUE(tileRole::IsUpright(TileStance::Prop));
    EXPECT_TRUE(tileRole::IsUpright(TileStance::Wall));
    EXPECT_TRUE(tileRole::IsUpright(TileStance::Structure));
}

TEST(TileRoleTest, YSortFlagsNoLongerAffectGeometry)
{
    // Regression guard for the rule this stance enum replaced: IsUpright used to
    // be `noProjection || ySortPlus || ySortMinus`, which stood up every flat
    // decal an author had tagged for sorting.
    //
    // The flat pipeline settles the argument - there the two y-sort flags change
    // draw ORDER only and never reach a geometry branch, so only noProjection has
    // ever meant "stands up". The flags keep that sorting meaning; they simply do
    // not reach this decision, and the signature no longer lets them.
    static_assert(std::is_same_v<decltype(tileRole::IsUpright(TileStance::Flat)), bool>);
    EXPECT_FALSE(tileRole::IsUpright(TileStance::Flat));
}

TEST(TileRoleTest, OnlyStructuresStackIntoATallBody)
{
    // Upright and STACKING are different questions, and conflating them turned a
    // fence running north-south into a tower: its posts were read as the rows of
    // one tall image and lifted onto each other. A Prop or a Wall is one tile
    // tall on its own row; only a Structure is a multi-tile-tall body.
    EXPECT_TRUE(tileRole::StacksVertically(TileStance::Structure));
    EXPECT_FALSE(tileRole::StacksVertically(TileStance::Prop));
    EXPECT_FALSE(tileRole::StacksVertically(TileStance::Wall));
    EXPECT_FALSE(tileRole::StacksVertically(TileStance::Flat));
}

TEST(TileRoleTest, StackingIsStrictlyNarrowerThanUprightness)
{
    // Every stacking tile is upright, but not every upright tile stacks. If the
    // two rules ever coincide again, one of them has lost its meaning.
    for (const TileStance stance : EnumValues<TileStance>())
    {
        if (tileRole::StacksVertically(stance))
        {
            EXPECT_TRUE(tileRole::IsUpright(stance)) << EnumTraits<TileStance>::ToString(stance);
        }
    }

    // The gap between them is real: a Prop stands up without stacking.
    EXPECT_TRUE(tileRole::IsUpright(TileStance::Prop));
    EXPECT_FALSE(tileRole::StacksVertically(TileStance::Prop));
}

TEST(TileRoleTest, SurfacesAreExactlyWallsAndStructures)
{
    // A surface is grid-locked and welds to its neighbours; a pole turns toward
    // the camera and stands alone. Which one a tile is used to be guessed from
    // whether it happened to touch something.
    EXPECT_TRUE(tileRole::IsSurface(TileStance::Wall));
    EXPECT_TRUE(tileRole::IsSurface(TileStance::Structure));
    EXPECT_FALSE(tileRole::IsSurface(TileStance::Prop));
    EXPECT_FALSE(tileRole::IsSurface(TileStance::Flat));
}

TEST(TileRoleTest, StanceCarriesNoLayerOrNeighbourInformation)
{
    // Regression guard for two rules that WERE tried and rejected:
    //
    //  - standing a tile up for sitting on a foreground layer. Foreground layers
    //    carry flat decals too, so that stood the wrong artwork on its edge.
    //  - deriving surface-vs-pole from adjacency. Two unrelated bushes placed
    //    side by side both stopped turning.
    //
    // Every predicate here takes exactly one stance and nothing else, so neither
    // input can be wired back in without deliberately changing these signatures.
    // If that ever happens, this test is where to argue about it.
    static_assert(std::is_invocable_v<decltype(tileRole::IsUpright), TileStance>);
    static_assert(std::is_invocable_v<decltype(tileRole::StacksVertically), TileStance>);
    static_assert(std::is_invocable_v<decltype(tileRole::IsSurface), TileStance>);

    // The load-time migration in LoadMapFromJSON does read the layer index and a
    // cell's neighbours, exactly once, to seed this field for legacy maps. That
    // is a one-off translation of old data, not a rule - nothing at render time
    // may consult either.
    EXPECT_FALSE(tileRole::IsUpright(TileStance::Flat));
}

TEST(TileRoleTest, PropsAlwaysTurnTowardTheCamera)
{
    // A lantern, a stone, a fence post is effectively a pole: turning it spins it
    // in place, so it never leaves the spot it was authored on. Nothing about its
    // surroundings changes that.
    EXPECT_GT(tileRole::DampingFor(TileStance::Prop, 1).yawFollow, 0.0f);
    EXPECT_GT(tileRole::DampingFor(TileStance::Prop, 8).yawFollow, 0.0f);
}

TEST(TileRoleTest, WallsAreAlwaysLockedToTheGrid)
{
    // A Wall is a SURFACE by authored intent, whatever its extent. Rotating a
    // surface drags its far end through world space; keeping the yaw fixed is
    // also what stops a run of them fanning open like venetian blinds.
    EXPECT_FLOAT_EQ(tileRole::DampingFor(TileStance::Wall, 1).yawFollow, 0.0f);
    EXPECT_FLOAT_EQ(tileRole::DampingFor(TileStance::Wall, 4).yawFollow, 0.0f);
}

TEST(TileRoleTest, StructuresStillDecideByTheirOwnWidth)
{
    // Unchanged behaviour, and deliberately so. This is not inference between
    // separate props - it is the extent of ONE authored body, so a narrow tower
    // (a totem, a tree painted as a tall column) keeps turning while a facade
    // stays anchored on its footprint.
    EXPECT_GT(tileRole::DampingFor(TileStance::Structure, 1).yawFollow, 0.0f);
    EXPECT_FLOAT_EQ(tileRole::DampingFor(TileStance::Structure, 3).yawFollow, 0.0f);
}

TEST(TileRoleTest, GridLockIsTheSingleSourceOfTruthForDamping)
{
    // RenderWorld3D picks between two pre-resolved orientations and DampingFor
    // builds the damping; both must agree, and neither may re-derive the rule.
    // Comparing a float yawFollow against zero to recover the answer would be
    // exactly the kind of duplicate that drifts.
    for (const TileStance stance : EnumValues<TileStance>())
    {
        for (const int width : {1, 2, 5})
        {
            const bool locked = tileRole::IsGridLocked(stance, width);
            const float yawFollow = tileRole::DampingFor(stance, width).yawFollow;
            EXPECT_EQ(locked, yawFollow == 0.0f)
                << EnumTraits<TileStance>::ToString(stance) << " width " << width;
        }
    }

    EXPECT_FALSE(tileRole::IsGridLocked(TileStance::Prop, 5));
    EXPECT_TRUE(tileRole::IsGridLocked(TileStance::Wall, 1));
    EXPECT_FALSE(tileRole::IsGridLocked(TileStance::Structure, 1));
    EXPECT_TRUE(tileRole::IsGridLocked(TileStance::Structure, 2));
}

TEST(TileRoleTest, WiderStructuresAreLockedToTheGrid)
{
    // A facade or railing is a SURFACE. Rotating it drags its far end through
    // world space, which is what made buildings and bridge borders wander off
    // their anchors while the camera orbited.
    for (const int width : {2, 3, 8})
    {
        EXPECT_FLOAT_EQ(tileRole::DampingForWidth(width).yawFollow, 0.0f) << "width " << width;
    }
}

TEST(TileRoleTest, WidthNeverAffectsTheLean)
{
    // Only the yaw is suppressed. The lean is what gives upright artwork its
    // apparent height, and it moves the quad's TOP, never its anchored base -
    // so a wall keeps leaning with the camera's pitch.
    EXPECT_FLOAT_EQ(tileRole::DampingForWidth(1).leanFollow,
                    tileRole::DampingForWidth(5).leanFollow);
    EXPECT_GT(tileRole::DampingForWidth(5).leanFollow, 0.0f);
}

TEST(TileRoleTest, NoStanceEverLeansDifferently)
{
    // Marking a tile as a pole or a surface must change only whether it turns.
    // If the lean differed, re-marking one tile of a run would shear it out of
    // line with its neighbours.
    const float propLean = tileRole::DampingFor(TileStance::Prop, 1).leanFollow;
    EXPECT_FLOAT_EQ(tileRole::DampingFor(TileStance::Wall, 1).leanFollow, propLean);
    EXPECT_FLOAT_EQ(tileRole::DampingFor(TileStance::Structure, 1).leanFollow, propLean);
    EXPECT_GT(propLean, 0.0f);
}

TEST(TileRoleTest, DegenerateWidthsAreTreatedAsAPole)
{
    // Guards the boundary: 0 or negative must not silently fall into the
    // grid-locked branch through a sign mistake.
    EXPECT_GT(tileRole::DampingForWidth(0).yawFollow, 0.0f);
    EXPECT_GT(tileRole::DampingForWidth(-3).yawFollow, 0.0f);
}

TEST(TileRoleTest, UprightTilesUseSceneryDamping)
{
    // Scenery under-follows yaw; only characters follow it completely. If tile
    // art ever followed fully, a row of fences would pivot in lockstep.
    const billboard::Damping damping = tileRole::UprightDamping();
    EXPECT_GT(damping.yawFollow, 0.0f);
    EXPECT_LT(damping.yawFollow, 1.0f);
    EXPECT_FLOAT_EQ(damping.yawFollow,
                    billboard::DefaultDamping(billboard::Role::Scenery).yawFollow);
}

TEST(TileRoleTest, StanceNamesRoundTrip)
{
    // The editor HUD and any future console command read these names, and the
    // Count/Names/static_assert trio is the checklist that bites ParticleType.
    for (const TileStance stance : EnumValues<TileStance>())
    {
        const std::string_view name = EnumTraits<TileStance>::ToString(stance);
        const std::optional<TileStance> parsed = EnumTraits<TileStance>::FromString(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(*parsed, stance);
    }

    EXPECT_EQ(EnumTraits<TileStance>::Count, 4u);
    EXPECT_FALSE(EnumTraits<TileStance>::FromString("NoProjection").has_value());
    EXPECT_FALSE(EnumTraits<TileStance>::FromString("Upright").has_value());
}

TEST(TileRoleTest, FlatIsTheDefaultStance)
{
    // Every per-tile array defaults its unset cells, and an unmarked tile must be
    // ground artwork. If Flat were not the zero value, a freshly resized map
    // would stand its whole floor on edge.
    EXPECT_EQ(std::to_underlying(TileStance::Flat), 0);
    EXPECT_EQ(TileStance{}, TileStance::Flat);
}
