#include <gtest/gtest.h>

#include "../src/Pathfinding.hpp"
#include "../src/Tilemap.hpp"

#include <glm/glm.hpp>

namespace
{
// Build a tilemap with the given size where every tile is navigable.
Tilemap MakeOpenMap(int width, int height)
{
    Tilemap m;
    m.SetTilemapSize(width, height, /*generateMap=*/false);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            m.SetNavigation(x, y, true);
        }
    }
    return m;
}
}  // namespace

TEST(PathfindingTests, FindPathStraightLine)
{
    Tilemap m = MakeOpenMap(10, 10);
    auto path = Pathfinding::FindPath(m, {0, 0}, {0, 4});
    ASSERT_EQ(path.size(), 5u);
    EXPECT_EQ(path.front(), glm::ivec2(0, 0));
    EXPECT_EQ(path.back(), glm::ivec2(0, 4));
}

TEST(PathfindingTests, FindPathRoutesAroundObstacle)
{
    Tilemap m = MakeOpenMap(5, 5);
    // Wall at column 2 except for a gap at row 0.
    for (int y = 1; y < 5; ++y)
    {
        m.SetNavigation(2, y, false);
    }
    auto path = Pathfinding::FindPath(m, {0, 4}, {4, 4});
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), glm::ivec2(0, 4));
    EXPECT_EQ(path.back(), glm::ivec2(4, 4));
    bool wentThroughGap = false;
    for (auto p : path)
    {
        if (p.x == 2 && p.y == 0)
        {
            wentThroughGap = true;
        }
    }
    EXPECT_TRUE(wentThroughGap);
}

TEST(PathfindingTests, FindPathReturnsEmptyWhenUnreachable)
{
    Tilemap m = MakeOpenMap(5, 5);
    for (int y = 0; y < 5; ++y)
    {
        m.SetNavigation(2, y, false);
    }
    auto path = Pathfinding::FindPath(m, {0, 0}, {4, 0});
    EXPECT_TRUE(path.empty());
}

TEST(PathfindingTests, FindPathRejectsOutOfBoundsStart)
{
    Tilemap m = MakeOpenMap(5, 5);
    auto path = Pathfinding::FindPath(m, {-1, 0}, {2, 2});
    EXPECT_TRUE(path.empty());
}

TEST(PathfindingTests, FloodReachableCountsAndBounds)
{
    Tilemap m;
    m.SetTilemapSize(10, 10, /*generateMap=*/false);
    for (int y = 3; y < 8; ++y)
    {
        for (int x = 2; x < 7; ++x)
        {
            m.SetNavigation(x, y, true);
        }
    }
    glm::ivec2 mn{}, mx{};
    auto count = Pathfinding::FloodReachable(m, {2, 3}, mn, mx);
    EXPECT_EQ(count, 25u);
    EXPECT_EQ(mn, glm::ivec2(2, 3));
    EXPECT_EQ(mx, glm::ivec2(6, 7));
}

TEST(PathfindingTests, FloodReachableNonNavigableStartReturnsZero)
{
    Tilemap m;
    m.SetTilemapSize(5, 5, /*generateMap=*/false);
    glm::ivec2 mn{}, mx{};
    auto count = Pathfinding::FloodReachable(m, {0, 0}, mn, mx);
    EXPECT_EQ(count, 0u);
}
