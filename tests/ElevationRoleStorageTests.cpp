// Storage guard for the per-layer elevation role.
//
// Separate from ElevationRoleTests.cpp on purpose: that file pins the pure height
// rule and deliberately includes no Tilemap, which is what lets it stand as proof
// the rule is reachable without a map or a renderer. This file is where the
// storage decision - per LAYER, unlike elevation itself which is per CELL - gets
// pinned instead.
#include "../src/ElevationRole.hpp"
#include "../src/Tilemap.hpp"

#include <gtest/gtest.h>

TEST(ElevationRoleStorageTest, RoleIsPerLayerNotPerCell)
{
    // The storage decision, pinned. One cell, two layers, two different roles -
    // this is what keeps water under a bridge while the deck above it rises.
    Tilemap tm;
    tm.SetTilemapSize(8, 8, false);

    tm.SetLayerElevationRole(3, 3, 0, ElevationRole::Ground);
    tm.SetLayerElevationRole(3, 3, 2, ElevationRole::Raised);

    EXPECT_EQ(tm.GetLayerElevationRole(3, 3, 0), ElevationRole::Ground);
    EXPECT_EQ(tm.GetLayerElevationRole(3, 3, 2), ElevationRole::Raised);
}

TEST(ElevationRoleStorageTest, ElevationItselfStaysPerCell)
{
    // The other half of the split: the HEIGHT is shared by every layer at a cell.
    // Only participation is per layer.
    Tilemap tm;
    tm.SetTilemapSize(8, 8, false);
    tm.SetElevation(3, 3, 6);

    EXPECT_EQ(tm.GetElevation(3, 3), 6);
    EXPECT_EQ(tm.GetLayerElevationRole(3, 3, 0), ElevationRole::Ground);
    EXPECT_EQ(tm.GetLayerElevationRole(3, 3, 2), ElevationRole::Ground);
}

TEST(ElevationRoleStorageTest, EveryCellStartsGround)
{
    Tilemap tm;
    tm.SetTilemapSize(4, 4, false);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            EXPECT_EQ(tm.GetLayerElevationRole(x, y, 0), ElevationRole::Ground)
                << "at (" << x << ", " << y << ")";
        }
    }
}

TEST(ElevationRoleStorageTest, ResizeKeepsTheArrayAddressable)
{
    // Guards the five-place checklist: a field missing from the resize_all fold
    // leaves a zero-length vector, and then every write is undefined behaviour on
    // the undersized vector rather than a graceful no-op. Writing to the far corner
    // of the last layer is what catches it.
    Tilemap tm;
    tm.SetTilemapSize(16, 16, false);
    tm.SetLayerElevationRole(15, 15, 9, ElevationRole::Ramp);
    EXPECT_EQ(tm.GetLayerElevationRole(15, 15, 9), ElevationRole::Ramp);
}

TEST(ElevationRoleStorageTest, ResizingAgainResetsToGround)
{
    // Guards the reset_all fold in TileLayer::Clear.
    Tilemap tm;
    tm.SetTilemapSize(8, 8, false);
    tm.SetLayerElevationRole(2, 2, 0, ElevationRole::Ramp);
    ASSERT_EQ(tm.GetLayerElevationRole(2, 2, 0), ElevationRole::Ramp);

    tm.SetTilemapSize(8, 8, false);
    EXPECT_EQ(tm.GetLayerElevationRole(2, 2, 0), ElevationRole::Ground);
}

TEST(ElevationRoleStorageTest, OutOfRangeReadsAreGroundNotACrash)
{
    Tilemap tm;
    tm.SetTilemapSize(4, 4, false);
    EXPECT_EQ(tm.GetLayerElevationRole(-1, 0, 0), ElevationRole::Ground);
    EXPECT_EQ(tm.GetLayerElevationRole(0, 99, 0), ElevationRole::Ground);
    EXPECT_EQ(tm.GetLayerElevationRole(0, 0, 99), ElevationRole::Ground);
}

TEST(ElevationRoleStorageTest, OutOfRangeWritesAreIgnoredNotACrash)
{
    Tilemap tm;
    tm.SetTilemapSize(4, 4, false);
    tm.SetLayerElevationRole(-1, 0, 0, ElevationRole::Raised);
    tm.SetLayerElevationRole(0, 0, 99, ElevationRole::Raised);
    EXPECT_EQ(tm.GetLayerElevationRole(0, 0, 0), ElevationRole::Ground);
}
