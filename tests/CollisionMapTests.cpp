#include <gtest/gtest.h>
#include "../src/CollisionMap.hpp"

#include <vector>

class CollisionMapTest : public ::testing::Test
{
protected:
    CollisionMap<std::vector> map;

    void SetUp() override
    {
        map.Resize(10, 10);
    }
};

// --- Basic Operations ---

TEST_F(CollisionMapTest, InitiallyEmpty)
{
    EXPECT_EQ(map.GetCollisionCount(), 0);
}

TEST_F(CollisionMapTest, GetWidth)
{
    EXPECT_EQ(map.GetWidth(), 10);
}

TEST_F(CollisionMapTest, GetHeight)
{
    EXPECT_EQ(map.GetHeight(), 10);
}

TEST_F(CollisionMapTest, SetCollision_Single)
{
    map.SetCollision(5, 5, true);
    EXPECT_TRUE(map.HasCollision(5, 5));
    EXPECT_EQ(map.GetCollisionCount(), 1);
}

TEST_F(CollisionMapTest, SetCollision_Multiple)
{
    map.SetCollision(0, 0, true);
    map.SetCollision(9, 9, true);
    map.SetCollision(5, 5, true);
    EXPECT_EQ(map.GetCollisionCount(), 3);
}

TEST_F(CollisionMapTest, SetCollision_ToggleOff)
{
    map.SetCollision(5, 5, true);
    EXPECT_TRUE(map.HasCollision(5, 5));
    map.SetCollision(5, 5, false);
    EXPECT_FALSE(map.HasCollision(5, 5));
}

TEST_F(CollisionMapTest, Clear_RemovesAll)
{
    map.SetCollision(0, 0, true);
    map.SetCollision(5, 5, true);
    map.SetCollision(9, 9, true);
    EXPECT_EQ(map.GetCollisionCount(), 3);
    map.Clear();
    EXPECT_EQ(map.GetCollisionCount(), 0);
}

// --- Bounds Handling ---

TEST_F(CollisionMapTest, HasCollision_OutOfBounds_ReturnsFalse)
{
    EXPECT_FALSE(map.HasCollision(-1, 0));
    EXPECT_FALSE(map.HasCollision(0, -1));
    EXPECT_FALSE(map.HasCollision(10, 0));
    EXPECT_FALSE(map.HasCollision(0, 10));
    EXPECT_FALSE(map.HasCollision(100, 100));
}

TEST_F(CollisionMapTest, SetCollision_OutOfBounds_Ignored)
{
    map.SetCollision(-1, 0, true);
    map.SetCollision(100, 100, true);
    EXPECT_EQ(map.GetCollisionCount(), 0);
}

// --- 2D Array Syntax ---

TEST_F(CollisionMapTest, OperatorBracket_Write)
{
    map[3][4] = true;
    EXPECT_TRUE(map.HasCollision(3, 4));
}

TEST_F(CollisionMapTest, OperatorBracket_Read)
{
    map.SetCollision(7, 8, true);
    bool val = map[7][8];
    EXPECT_TRUE(val);
}

TEST_F(CollisionMapTest, OperatorBracket_OutOfBounds_ReadReturnsFalse)
{
    bool val = map[-1][0];
    EXPECT_FALSE(val);
    val = map[100][100];
    EXPECT_FALSE(val);
}

TEST_F(CollisionMapTest, OperatorBracket_OutOfBounds_WriteIgnored)
{
    map[-1][0] = true;
    map[100][100] = true;
    EXPECT_EQ(map.GetCollisionCount(), 0);
}

// --- Row-Major Layout ---

TEST_F(CollisionMapTest, RowMajorLayout_IndexCalculation)
{
    // Set collision at (3, 2), which should be at index 2*10+3 = 23
    map.SetCollision(3, 2, true);
    auto indices = map.GetCollisionIndices();
    ASSERT_EQ(indices.size(), 1);
    EXPECT_EQ(indices[0], 23);
}

TEST_F(CollisionMapTest, GetCollisionIndices_Multiple)
{
    map.SetCollision(0, 0, true); // index 0
    map.SetCollision(9, 0, true); // index 9
    map.SetCollision(0, 1, true); // index 10

    auto indices = map.GetCollisionIndices();
    ASSERT_EQ(indices.size(), 3);

    // Indices should be in order
    EXPECT_EQ(indices[0], 0);
    EXPECT_EQ(indices[1], 9);
    EXPECT_EQ(indices[2], 10);
}

// --- Resize ---

TEST_F(CollisionMapTest, Resize_UpdatesDimensions)
{
    map.Resize(20, 20);
    EXPECT_EQ(map.GetWidth(), 20);
    EXPECT_EQ(map.GetHeight(), 20);
}

TEST_F(CollisionMapTest, Resize_LargerAllowsNewArea)
{
    map.Resize(100, 100);
    // Can now set in larger area
    map.SetCollision(99, 99, true);
    EXPECT_TRUE(map.HasCollision(99, 99));
}

// --- SetData ---

TEST_F(CollisionMapTest, SetData_ValidSize)
{
    std::vector<bool> data(25, false);
    data[12] = true; // Center of 5x5

    EXPECT_TRUE(map.SetData(data, 5, 5));
    EXPECT_EQ(map.GetWidth(), 5);
    EXPECT_EQ(map.GetHeight(), 5);
    EXPECT_TRUE(map.HasCollision(2, 2)); // 12 = 2*5+2
}

TEST_F(CollisionMapTest, SetData_InvalidSize_Rejected)
{
    std::vector<bool> data(10, false);
    EXPECT_FALSE(map.SetData(data, 5, 5)); // 10 != 25
    // Original dimensions unchanged
    EXPECT_EQ(map.GetWidth(), 10);
    EXPECT_EQ(map.GetHeight(), 10);
}

TEST_F(CollisionMapTest, SetData_NegativeDimensions_Rejected)
{
    std::vector<bool> data;
    EXPECT_FALSE(map.SetData(data, -1, 0));
    EXPECT_FALSE(map.SetData(data, 0, -1));
    EXPECT_EQ(map.GetWidth(), 10);
    EXPECT_EQ(map.GetHeight(), 10);
}

// --- Copy/Move ---

TEST_F(CollisionMapTest, CopyConstructor)
{
    map.SetCollision(5, 5, true);
    CollisionMap<std::vector> copy(map);
    EXPECT_TRUE(copy.HasCollision(5, 5));
    EXPECT_EQ(copy.GetWidth(), 10);
}

TEST_F(CollisionMapTest, MoveConstructor)
{
    map.SetCollision(5, 5, true);
    CollisionMap<std::vector> moved(std::move(map));
    EXPECT_TRUE(moved.HasCollision(5, 5));
}

TEST_F(CollisionMapTest, CopyAssignment)
{
    map.SetCollision(5, 5, true);
    CollisionMap<std::vector> other;
    other = map;
    EXPECT_TRUE(other.HasCollision(5, 5));
}

// --- Edge Cases ---

TEST_F(CollisionMapTest, ZeroSizedMap)
{
    CollisionMap<std::vector> empty;
    EXPECT_EQ(empty.GetWidth(), 0);
    EXPECT_EQ(empty.GetHeight(), 0);
    EXPECT_FALSE(empty.HasCollision(0, 0));
    EXPECT_EQ(empty.GetCollisionCount(), 0);
}

TEST_F(CollisionMapTest, SingleCellMap)
{
    CollisionMap<std::vector> single;
    single.Resize(1, 1);
    EXPECT_FALSE(single.HasCollision(0, 0));
    single.SetCollision(0, 0, true);
    EXPECT_TRUE(single.HasCollision(0, 0));
    EXPECT_EQ(single.GetCollisionCount(), 1);
}
