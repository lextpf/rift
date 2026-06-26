#include <gtest/gtest.h>

#include "../src/RenderDrawable.hpp"

#include <ecs.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

// Phase 4a (ECS pre-integration): the Y-sorted render pass moved from an
// enum-keyed RenderItem + an inline comparator to a type-agnostic Drawable +
// DrawableDepthLess / AddNpcDrawables / AddPlayerDrawables (a behavior-identical
// port - tieBias carries the old enum's integer value). These tests are the
// golden net that locks the ordering and the entity split so the refactor is
// provably equivalent to the original enum comparator. Pure data paths - no
// renderer; the half-draw thunk is never invoked here, only the build-time
// fields (sortY/tieBias/topHalf/world/handle) are asserted.

namespace
{
Drawable MakeEntity(float sortY, std::uint8_t tieBias)
{
    Drawable d;
    d.cls = DrawableClass::Entity;
    d.sortY = sortY;
    d.tieBias = tieBias;
    return d;
}

Drawable MakeTile(float sortY, bool ySortMinus)
{
    Drawable d;
    d.cls = DrawableClass::Tile;
    d.sortY = sortY;
    d.isYSortMinus = ySortMinus;
    d.tieBias = TIE_TILE;
    return d;
}
}  // namespace

TEST(RenderSort, LowerYDrawsFirst)
{
    const Drawable lower = MakeEntity(50.0f, TIE_NPC_BOTTOM);
    const Drawable higher = MakeEntity(100.0f, TIE_NPC_BOTTOM);
    EXPECT_TRUE(DrawableDepthLess(lower, higher));
    EXPECT_FALSE(DrawableDepthLess(higher, lower));
}

TEST(RenderSort, EqualDepthTileSortsBeforeEntity)
{
    // At identical depth, higher tieBias draws first (TILE > entity), so a tile
    // sorts ahead of an entity -> entity renders on top of terrain.
    const Drawable tile = MakeTile(100.0f, /*ySortMinus=*/false);
    const Drawable entity = MakeEntity(100.0f, TIE_PLAYER_BOTTOM);
    EXPECT_TRUE(DrawableDepthLess(tile, entity));
    EXPECT_FALSE(DrawableDepthLess(entity, tile));
}

TEST(RenderSort, EqualDepthNpcSortsBeforePlayer)
{
    const Drawable npc = MakeEntity(100.0f, TIE_NPC_BOTTOM);
    const Drawable player = MakeEntity(100.0f, TIE_PLAYER_BOTTOM);
    EXPECT_TRUE(DrawableDepthLess(npc, player));
    EXPECT_FALSE(DrawableDepthLess(player, npc));
}

TEST(RenderSort, YSortMinusTileGetsHalfTileOffsetVsEntity)
{
    // A ysortMinus tile anchored at Y=96 compares at 96+8=104, so an entity at
    // Y=100 sorts before it (entity in front).
    const Drawable tile = MakeTile(96.0f, /*ySortMinus=*/true);
    const Drawable entity = MakeEntity(100.0f, TIE_NPC_BOTTOM);
    EXPECT_TRUE(DrawableDepthLess(entity, tile));
    EXPECT_FALSE(DrawableDepthLess(tile, entity));
}

TEST(RenderSort, FullSceneOrdersTileThenNpcThenPlayer)
{
    std::vector<Drawable> list{
        MakeEntity(100.0f, TIE_PLAYER_BOTTOM),
        MakeTile(100.0f, false),
        MakeEntity(100.0f, TIE_NPC_BOTTOM),
    };
    std::stable_sort(list.begin(), list.end(), DrawableDepthLess);
    EXPECT_EQ(list[0].cls, DrawableClass::Tile);
    EXPECT_EQ(list[1].tieBias, TIE_NPC_BOTTOM);
    EXPECT_EQ(list[2].tieBias, TIE_PLAYER_BOTTOM);
}

TEST(RenderSort, AddNpcDrawablesSplitsNpcWithTopOffset)
{
    ecs::registry world;
    const ecs::entity npc{};  // build path never dereferences the handle
    std::vector<Drawable> list;
    constexpr float topOffset = 8.0f;
    AddNpcDrawables(
        list, world, npc, glm::vec2(40.0f, 200.0f), topOffset, TIE_NPC_BOTTOM, TIE_NPC_TOP);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(2));
    EXPECT_FALSE(list[0].topHalf);
    EXPECT_FLOAT_EQ(list[0].sortY, 200.0f);
    EXPECT_EQ(list[0].tieBias, TIE_NPC_BOTTOM);
    EXPECT_TRUE(list[1].topHalf);
    EXPECT_FLOAT_EQ(list[1].sortY, 200.0f - topOffset);
    EXPECT_EQ(list[1].tieBias, TIE_NPC_TOP);
    EXPECT_EQ(list[0].world, &world);
    EXPECT_EQ(list[1].world, &world);
    EXPECT_EQ(list[0].handle, npc);
    EXPECT_EQ(list[1].handle, npc);
}

TEST(RenderSort, AddPlayerDrawablesHalvesShareDepth)
{
    ecs::registry world;
    const ecs::entity player{};
    std::vector<Drawable> list;
    AddPlayerDrawables(
        list, world, player, glm::vec2(0.0f, 150.0f), 0.0f, TIE_PLAYER_BOTTOM, TIE_PLAYER_TOP);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(2));
    EXPECT_FLOAT_EQ(list[0].sortY, 150.0f);
    EXPECT_FLOAT_EQ(list[1].sortY, 150.0f);
}
