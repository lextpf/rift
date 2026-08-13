#include <gtest/gtest.h>

#include "../src/RenderDrawable.hpp"

#include <ecs.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

// Pure render-queue tests: no renderer is invoked. They lock the ordinary
// Y-sort rules, structure-local support constraints, and the invariant that a
// character contributes one atomic queue item.

namespace
{
Drawable MakeEntity(float sortY,
                    std::uint8_t tieBias,
                    float supportHeight = 0.0f,
                    SupportSurface supportSurface = SupportSurface::Ground,
                    int surfaceRegionId = -1)
{
    Drawable d;
    d.cls = DrawableClass::Entity;
    d.phase = DrawablePhase::YSorted;
    d.sortY = sortY;
    d.supportHeight = supportHeight;
    d.supportSurface = supportSurface;
    d.surfaceRegionId = surfaceRegionId;
    d.tieBias = tieBias;
    return d;
}

Drawable MakeTile(float sortY,
                  bool ySortMinus,
                  float supportHeight = 0.0f,
                  DrawablePhase phase = DrawablePhase::YSorted,
                  int surfaceRegionId = -1,
                  bool authoredYSort = false)
{
    Drawable d;
    d.cls = DrawableClass::Tile;
    d.phase = phase;
    d.sortY = sortY;
    d.supportHeight = supportHeight;
    d.surfaceRegionId = surfaceRegionId;
    d.isYSortMinus = ySortMinus;
    d.tieBias = TIE_TILE;
    d.tile.authoredYSort = authoredYSort;
    d.tile.surfaceRegionId = surfaceRegionId;
    return d;
}
}  // namespace

TEST(RenderSort, LowerYDrawsFirst)
{
    const Drawable lower = MakeEntity(50.0f, TIE_NPC);
    const Drawable higher = MakeEntity(100.0f, TIE_NPC);
    EXPECT_TRUE(DrawableDepthLess(lower, higher));
    EXPECT_FALSE(DrawableDepthLess(higher, lower));
}

TEST(RenderSort, EqualDepthTileSortsBeforeEntity)
{
    // At identical depth, higher tieBias draws first (TILE > entity), so a tile
    // sorts ahead of an entity -> entity renders on top of terrain.
    const Drawable tile = MakeTile(100.0f, /*ySortMinus=*/false);
    const Drawable entity = MakeEntity(100.0f, TIE_PLAYER);
    EXPECT_TRUE(DrawableDepthLess(tile, entity));
    EXPECT_FALSE(DrawableDepthLess(entity, tile));
}

TEST(RenderSort, EqualDepthNpcSortsBeforePlayer)
{
    const Drawable npc = MakeEntity(100.0f, TIE_NPC);
    const Drawable player = MakeEntity(100.0f, TIE_PLAYER);
    EXPECT_TRUE(DrawableDepthLess(npc, player));
    EXPECT_FALSE(DrawableDepthLess(player, npc));
}

TEST(RenderSort, YSortMinusTileGetsHalfTileOffsetVsEntity)
{
    // A ysortMinus tile anchored at Y=96 compares at 96+8=104, so an entity at
    // Y=100 sorts before it and the tile is drawn last (tile in front).
    const Drawable tile = MakeTile(96.0f, /*ySortMinus=*/true);
    const Drawable entity = MakeEntity(100.0f, TIE_NPC);
    EXPECT_TRUE(DrawableDepthLess(entity, tile));
    EXPECT_FALSE(DrawableDepthLess(tile, entity));
}

TEST(RenderSort, FullSceneOrdersTileThenNpcThenPlayer)
{
    std::vector<Drawable> list{
        MakeEntity(100.0f, TIE_PLAYER),
        MakeTile(100.0f, false),
        MakeEntity(100.0f, TIE_NPC),
    };
    SortDrawables(list);
    EXPECT_EQ(list[0].cls, DrawableClass::Tile);
    EXPECT_EQ(list[1].tieBias, TIE_NPC);
    EXPECT_EQ(list[2].tieBias, TIE_PLAYER);
}

TEST(RenderSort, AddNpcDrawableIsAtomic)
{
    ecs::registry world;
    const ecs::entity npc{};  // build path never dereferences the handle
    std::vector<Drawable> list;
    AddNpcDrawable(
        list, world, npc, glm::vec2(40.0f, 200.0f), SupportSurface::Elevation, 10.0f, 7, TIE_NPC);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(1));
    EXPECT_FLOAT_EQ(list[0].sortY, 200.0f);
    EXPECT_FLOAT_EQ(list[0].supportHeight, 10.0f);
    EXPECT_EQ(list[0].phase, DrawablePhase::YSorted);
    EXPECT_EQ(list[0].surfaceRegionId, 7);
    EXPECT_EQ(list[0].supportSurface, SupportSurface::Elevation);
    EXPECT_EQ(list[0].tieBias, TIE_NPC);
    EXPECT_EQ(list[0].world, &world);
    EXPECT_EQ(list[0].handle, npc);
    EXPECT_NE(list[0].drawEntity, nullptr);
}

TEST(RenderSort, AddPlayerDrawableIsAtomic)
{
    ecs::registry world;
    const ecs::entity player{};
    std::vector<Drawable> list;
    AddPlayerDrawable(
        list, world, player, glm::vec2(0.0f, 150.0f), SupportSurface::Ground, 0.0f, -1, TIE_PLAYER);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(1));
    EXPECT_FLOAT_EQ(list[0].sortY, 150.0f);
    EXPECT_FLOAT_EQ(list[0].supportHeight, 0.0f);
    EXPECT_EQ(list[0].phase, DrawablePhase::YSorted);
    EXPECT_NE(list[0].drawEntity, nullptr);
}

TEST(RenderSort, ActorBesideRampPreservesForegroundYSortRule)
{
    // This mirrors the real ramp's bottom foreground Y-sort row: the actor and
    // ramp share an authored anchor, but the actor's feet are outside region 7.
    // Elevation metadata must not push the ramp 10px forward and cover the actor.
    Drawable actor = MakeEntity(1760.0f, TIE_PLAYER);
    Drawable ramp = MakeTile(1760.0f, false, 10.0f, DrawablePhase::YSorted, 7, true);

    std::vector<Drawable> list{actor, ramp};
    SortDrawables(list);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(list[0].cls, DrawableClass::Tile);
    EXPECT_EQ(list[0].phase, DrawablePhase::YSorted);
    EXPECT_EQ(list[1].cls, DrawableClass::Entity);
}

TEST(RenderSort, GroundActorInsideFootprintDrawsBeforeWholeBridge)
{
    constexpr int region = 7;
    Drawable groundActor = MakeEntity(1000.0f, TIE_PLAYER, 0.0f, SupportSurface::Ground, region);
    Drawable bridgeTop = MakeTile(-500.0f, false, 10.0f, DrawablePhase::YSorted, region, true);
    Drawable bridgeFloor = MakeTile(100.0f, false, 10.0f, DrawablePhase::Background, region);
    Drawable bridgeBottom = MakeTile(1500.0f, false, 10.0f, DrawablePhase::YSorted, region, true);

    std::vector<Drawable> list{bridgeFloor, bridgeBottom, groundActor, bridgeTop};
    SortDrawables(list);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(list[0].cls, DrawableClass::Entity);
    EXPECT_EQ(list[0].supportSurface, SupportSurface::Ground);
    EXPECT_EQ(list[1].cls, DrawableClass::Tile);
    EXPECT_EQ(list[2].cls, DrawableClass::Tile);
    EXPECT_EQ(list[3].cls, DrawableClass::Tile);
}

TEST(RenderSort, DeckActorPreservesAuthoredTopAndBottomRailings)
{
    constexpr int region = 7;
    Drawable floor = MakeTile(0.0f, false, 10.0f, DrawablePhase::Background, region);
    Drawable topRailing = MakeTile(80.0f, false, 10.0f, DrawablePhase::YSorted, region, true);
    Drawable actor = MakeEntity(100.0f, TIE_PLAYER, 10.0f, SupportSurface::Elevation, region);
    Drawable bottomRailing = MakeTile(120.0f, false, 10.0f, DrawablePhase::YSorted, region, true);

    std::vector<Drawable> list{bottomRailing, actor, floor, topRailing};
    SortDrawables(list);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(list[0].phase, DrawablePhase::Background);
    EXPECT_FLOAT_EQ(list[1].sortY, 80.0f);
    EXPECT_EQ(list[2].cls, DrawableClass::Entity);
    EXPECT_FLOAT_EQ(list[3].sortY, 120.0f);
}

TEST(RenderSort, GroundAndDeckActorsFormLocalUnderSurfaceOverChain)
{
    constexpr int region = 7;
    Drawable groundActor = MakeEntity(500.0f, TIE_PLAYER, 0.0f, SupportSurface::Ground, region);
    Drawable floor = MakeTile(-500.0f, false, 10.0f, DrawablePhase::Background, region);
    Drawable deckActor = MakeEntity(-1000.0f, TIE_NPC, 10.0f, SupportSurface::Elevation, region);

    std::vector<Drawable> list{deckActor, floor, groundActor};
    SortDrawables(list);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(list[0].supportSurface, SupportSurface::Ground);
    EXPECT_EQ(list[1].phase, DrawablePhase::Background);
    EXPECT_EQ(list[2].supportSurface, SupportSurface::Elevation);
}

TEST(RenderSort, AuthoredYSortMinusStillOccludesOnDeck)
{
    Drawable actor = MakeEntity(100.0f, TIE_PLAYER, 10.0f, SupportSurface::Elevation, 7);
    Drawable railing = MakeTile(96.0f, true, 10.0f, DrawablePhase::Background, 7, true);

    std::vector<Drawable> list{railing, actor};
    SortDrawables(list);

    ASSERT_EQ(list.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(list[0].cls, DrawableClass::Entity);
    EXPECT_EQ(list[1].cls, DrawableClass::Tile);
}

TEST(RenderSort, PhaseSelectionPreservesLayerAuthoring)
{
    Tilemap::DepthSortedTile tile;
    tile.authoredYSort = true;
    tile.surfaceRegionId = -1;
    EXPECT_EQ(TileDrawablePhase(tile), DrawablePhase::YSorted);

    tile.surfaceRegionId = 7;
    tile.isBackground = true;
    EXPECT_EQ(TileDrawablePhase(tile), DrawablePhase::YSorted);

    tile.isBackground = false;
    EXPECT_EQ(TileDrawablePhase(tile), DrawablePhase::YSorted);

    tile.authoredYSort = false;
    tile.surfaceRegionId = 7;
    tile.isBackground = true;
    EXPECT_EQ(TileDrawablePhase(tile), DrawablePhase::Background);

    tile.isBackground = false;
    EXPECT_EQ(TileDrawablePhase(tile), DrawablePhase::Foreground);
}
