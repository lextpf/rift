// Pure-logic guard for the per-layer elevation role. No GL/Vulkan context and no
// Tilemap: this file is the reason the ramp rule lives in a header rather than
// inside RenderWorld3D or EditorInput.cpp, neither of which is reachable from a test.
#include "../src/ElevationRole.hpp"

#include <gtest/gtest.h>

TEST(ElevationRoleTest, GroundIgnoresItsCellElevation)
{
    // The default. A layer marked Ground sits on the ground plane even where the
    // cell is elevated - that is what keeps water under a bridge instead of
    // carrying it up with the deck.
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(0, ElevationRole::Ground), 0.0f);
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(6, ElevationRole::Ground), 0.0f);
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(-8, ElevationRole::Ground), 0.0f);
}

TEST(ElevationRoleTest, RaisedIsExactlyItsCellElevation)
{
    // 1:1 in pixels, no scale factor - actors already use this exact value via
    // Elevation::offset, so ground and actors agree by construction.
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(6, ElevationRole::Raised), 6.0f);
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(10, ElevationRole::Raised), 10.0f);
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(-4, ElevationRole::Raised), -4.0f);
}

TEST(ElevationRoleTest, ARampCellIsRaisedToo)
{
    EXPECT_FLOAT_EQ(elevationRole::SurfaceHeight(4, ElevationRole::Ramp), 4.0f);
}

TEST(ElevationRoleTest, RampMeetsARaisedNeighbourAtTheNeighboursHeight)
{
    // The seam that matters: a ramp running into a deck must land on the deck
    // exactly, not at an average, or the bridge shows a step where they meet.
    const elevationRole::NeighbourSurface deck{6, ElevationRole::Raised};
    EXPECT_FLOAT_EQ(elevationRole::EdgeHeight(4, deck), 6.0f);
}

TEST(ElevationRoleTest, RampMeetsBareGroundAtZero)
{
    const elevationRole::NeighbourSurface grass{0, ElevationRole::Ground};
    EXPECT_FLOAT_EQ(elevationRole::EdgeHeight(2, grass), 0.0f);

    // A Ground neighbour contributes 0 even if its cell carries an elevation -
    // that layer is not participating.
    const elevationRole::NeighbourSurface unmarked{6, ElevationRole::Ground};
    EXPECT_FLOAT_EQ(elevationRole::EdgeHeight(2, unmarked), 0.0f);
}

TEST(ElevationRoleTest, RampMeetsARampNeighbourAtTheAverage)
{
    // Two ramp cells share their midpoint, which is what makes a multi-cell ramp
    // continuous instead of a staircase.
    const elevationRole::NeighbourSurface other{2, ElevationRole::Ramp};
    EXPECT_FLOAT_EQ(elevationRole::EdgeHeight(4, other), 3.0f);
}

TEST(ElevationRoleTest, TheShippedBridgeRampIsContinuous)
{
    // The real authored data, reproduced cell for cell: ground 0, ramps 2 and 4,
    // deck 6, along the X axis. Each cell's high edge must equal the next cell's
    // low edge, and the run must land on the deck exactly.
    const elevationRole::NeighbourSurface ground{0, ElevationRole::Ground};
    const elevationRole::NeighbourSurface ramp2{2, ElevationRole::Ramp};
    const elevationRole::NeighbourSurface ramp4{4, ElevationRole::Ramp};
    const elevationRole::NeighbourSurface deck{6, ElevationRole::Raised};

    const float cellA_low = elevationRole::EdgeHeight(2, ground);
    const float cellA_high = elevationRole::EdgeHeight(2, ramp4);
    const float cellB_low = elevationRole::EdgeHeight(4, ramp2);
    const float cellB_high = elevationRole::EdgeHeight(4, deck);

    EXPECT_FLOAT_EQ(cellA_low, 0.0f);
    EXPECT_FLOAT_EQ(cellA_high, 3.0f);
    EXPECT_FLOAT_EQ(cellB_low, 3.0f);   // same seam as cellA_high
    EXPECT_FLOAT_EQ(cellB_high, 6.0f);  // lands on the deck exactly
    EXPECT_FLOAT_EQ(cellA_high, cellB_low);
}

TEST(ElevationRoleTest, RoleNamesRoundTrip)
{
    for (const ElevationRole role : EnumValues<ElevationRole>())
    {
        const std::string_view name = EnumTraits<ElevationRole>::ToString(role);
        const std::optional<ElevationRole> parsed = EnumTraits<ElevationRole>::FromString(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(*parsed, role);
    }

    EXPECT_EQ(EnumTraits<ElevationRole>::Count, 3u);
    EXPECT_FALSE(EnumTraits<ElevationRole>::FromString("Elevated").has_value());
}

TEST(ElevationRoleTest, GroundIsTheDefaultRole)
{
    // Every per-tile array default-initialises; if Ground were not zero a freshly
    // resized map would come up as a grid of ramps.
    EXPECT_EQ(std::to_underlying(ElevationRole::Ground), 0);
    EXPECT_EQ(ElevationRole{}, ElevationRole::Ground);
}
