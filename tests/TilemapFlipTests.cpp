// Tests for per-tile flipX/flipY fields, the ReflectClipboardRegion math,
// and the editor commands that propagate them. No GL/Vulkan context is
// created here; we exercise data paths only.

#include <gtest/gtest.h>

#include "../src/EditorCommand.h"
#include "../src/EditorCommands.h"
#include "../src/NonPlayerCharacter.h"
#include "../src/Tilemap.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace
{
constexpr int kMapSize = 8;

// Saved data that survives a snapshot -> reflect -> paste round-trip in tests
struct CellState
{
    int tileId;
    float rotation;
    bool flipX;
    bool flipY;
};

void SetCell(Tilemap& tm, int x, int y, std::size_t layer, const CellState& s)
{
    tm.SetLayerTile(x, y, layer, s.tileId);
    tm.SetLayerRotation(x, y, layer, s.rotation);
    tm.SetLayerFlipX(x, y, layer, s.flipX);
    tm.SetLayerFlipY(x, y, layer, s.flipY);
}

CellState GetCell(const Tilemap& tm, int x, int y, std::size_t layer)
{
    return CellState{tm.GetLayerTile(x, y, layer),
                     tm.GetLayerRotation(x, y, layer),
                     tm.GetLayerFlipX(x, y, layer),
                     tm.GetLayerFlipY(x, y, layer)};
}
}  // namespace

class TilemapFlipTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(kMapSize, kMapSize, false); }
};

// --- Accessor round-trip -----------------------------------------------------

TEST_F(TilemapFlipTest, FlipFlagsDefaultToFalse)
{
    EXPECT_FALSE(tilemap.GetLayerFlipX(2, 3, 0));
    EXPECT_FALSE(tilemap.GetLayerFlipY(2, 3, 0));
}

TEST_F(TilemapFlipTest, SetGetFlipX_RoundTrip)
{
    tilemap.SetLayerFlipX(2, 3, 1, true);
    EXPECT_TRUE(tilemap.GetLayerFlipX(2, 3, 1));
    EXPECT_FALSE(tilemap.GetLayerFlipY(2, 3, 1));  // unchanged

    tilemap.SetLayerFlipX(2, 3, 1, false);
    EXPECT_FALSE(tilemap.GetLayerFlipX(2, 3, 1));
}

TEST_F(TilemapFlipTest, SetGetFlipY_RoundTrip)
{
    tilemap.SetLayerFlipY(4, 5, 2, true);
    EXPECT_TRUE(tilemap.GetLayerFlipY(4, 5, 2));
    EXPECT_FALSE(tilemap.GetLayerFlipX(4, 5, 2));  // unchanged
}

TEST_F(TilemapFlipTest, OutOfBoundsAccessReturnsDefault)
{
    EXPECT_FALSE(tilemap.GetLayerFlipX(-1, 0, 0));
    EXPECT_FALSE(tilemap.GetLayerFlipY(0, kMapSize, 0));
}

// --- PlaceTilesCmd preserves flip flags --------------------------------------

TEST_F(TilemapFlipTest, PlaceTilesCmd_AppliesAndRevertsFlipFlags)
{
    SetCell(tilemap, 1, 1, 0, {-1, 0.0f, false, false});

    PlaceTilesCmd::Entry entry{};
    entry.tileX = 1;
    entry.tileY = 1;
    entry.layer = 0;
    entry.oldTileId = -1;
    entry.newTileId = 42;
    entry.oldRotation = 0.0f;
    entry.newRotation = 270.0f;
    entry.oldFlipX = false;
    entry.newFlipX = true;
    entry.oldFlipY = false;
    entry.newFlipY = true;

    PlaceTilesCmd cmd{std::vector<PlaceTilesCmd::Entry>{entry}};
    cmd.Apply(tilemap, npcs);
    auto after = GetCell(tilemap, 1, 1, 0);
    EXPECT_EQ(after.tileId, 42);
    EXPECT_FLOAT_EQ(after.rotation, 270.0f);
    EXPECT_TRUE(after.flipX);
    EXPECT_TRUE(after.flipY);

    cmd.Revert(tilemap, npcs);
    auto restored = GetCell(tilemap, 1, 1, 0);
    EXPECT_EQ(restored.tileId, -1);
    EXPECT_FLOAT_EQ(restored.rotation, 0.0f);
    EXPECT_FALSE(restored.flipX);
    EXPECT_FALSE(restored.flipY);
}

// --- Clipboard round-trip preserves flip flags -------------------------------

TEST_F(TilemapFlipTest, SnapshotAndPaste_PreservesFlipFlags)
{
    SetCell(tilemap, 0, 0, 0, {7, 90.0f, true, false});
    SetCell(tilemap, 1, 0, 0, {8, 180.0f, false, true});

    ClipboardRegion region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 2, 1);
    ASSERT_EQ(region.cells.size(), 2u);
    EXPECT_TRUE(region.cells[0].layers[0].flipX);
    EXPECT_FALSE(region.cells[0].layers[0].flipY);
    EXPECT_FALSE(region.cells[1].layers[0].flipX);
    EXPECT_TRUE(region.cells[1].layers[0].flipY);

    // Wipe the source area and paste back at offset 4 to verify writes.
    SetCell(tilemap, 4, 0, 0, {-1, 0.0f, false, false});
    SetCell(tilemap, 5, 0, 0, {-1, 0.0f, false, false});

    PasteRegionCmd paste{4, 0, region};
    paste.Apply(tilemap, npcs);

    auto pasted0 = GetCell(tilemap, 4, 0, 0);
    auto pasted1 = GetCell(tilemap, 5, 0, 0);
    EXPECT_EQ(pasted0.tileId, 7);
    EXPECT_TRUE(pasted0.flipX);
    EXPECT_FALSE(pasted0.flipY);
    EXPECT_EQ(pasted1.tileId, 8);
    EXPECT_FALSE(pasted1.flipX);
    EXPECT_TRUE(pasted1.flipY);
}

// --- ReflectClipboardRegion math --------------------------------------------

TEST_F(TilemapFlipTest, ReflectX_SwapsColumnsAndTogglesFlipX)
{
    // 3x2 region, layer 0:
    //   (0,0)=A  (1,0)=B  (2,0)=C
    //   (0,1)=D  (1,1)=E  (2,1)=F
    SetCell(tilemap, 0, 0, 0, {1, 0.0f, false, false});
    SetCell(tilemap, 1, 0, 0, {2, 0.0f, true, false});
    SetCell(tilemap, 2, 0, 0, {3, 0.0f, false, false});
    SetCell(tilemap, 0, 1, 0, {4, 0.0f, false, false});
    SetCell(tilemap, 1, 1, 0, {5, 0.0f, false, true});
    SetCell(tilemap, 2, 1, 0, {6, 0.0f, false, false});

    ClipboardRegion region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 3, 2);
    ReflectClipboardRegion(region, /*flipXAxis=*/true);

    // After X-reflect: columns swap (C, B, A) and (F, E, D); each cell's
    // flipX is toggled while flipY stays.
    EXPECT_EQ(region.cells[0].layers[0].tileId, 3);  // was at (2,0)
    EXPECT_EQ(region.cells[1].layers[0].tileId, 2);  // unchanged column-wise
    EXPECT_EQ(region.cells[2].layers[0].tileId, 1);  // was at (0,0)
    EXPECT_EQ(region.cells[3].layers[0].tileId, 6);
    EXPECT_EQ(region.cells[4].layers[0].tileId, 5);
    EXPECT_EQ(region.cells[5].layers[0].tileId, 4);

    // flipX toggles for ALL cells; flipY untouched.
    EXPECT_TRUE(region.cells[0].layers[0].flipX);   // was false
    EXPECT_FALSE(region.cells[1].layers[0].flipX);  // was true (B)
    EXPECT_TRUE(region.cells[2].layers[0].flipX);   // was false
    EXPECT_TRUE(region.cells[4].layers[0].flipY);   // E's flipY preserved
}

TEST_F(TilemapFlipTest, ReflectY_SwapsRowsAndTogglesFlipY)
{
    SetCell(tilemap, 0, 0, 0, {1, 0.0f, false, false});
    SetCell(tilemap, 1, 0, 0, {2, 0.0f, false, false});
    SetCell(tilemap, 0, 1, 0, {3, 0.0f, true, false});
    SetCell(tilemap, 1, 1, 0, {4, 0.0f, false, true});

    ClipboardRegion region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 2, 2);
    ReflectClipboardRegion(region, /*flipXAxis=*/false);

    // After Y-reflect: rows swap; flipY toggles for all; flipX preserved.
    EXPECT_EQ(region.cells[0].layers[0].tileId, 3);
    EXPECT_EQ(region.cells[1].layers[0].tileId, 4);
    EXPECT_EQ(region.cells[2].layers[0].tileId, 1);
    EXPECT_EQ(region.cells[3].layers[0].tileId, 2);

    EXPECT_TRUE(region.cells[0].layers[0].flipX);   // was true (3)
    EXPECT_TRUE(region.cells[0].layers[0].flipY);   // toggled false -> true
    EXPECT_FALSE(region.cells[1].layers[0].flipY);  // was true (4) -> false
}

TEST_F(TilemapFlipTest, ReflectRotation_NegatesAcrossAxis)
{
    const float angles[] = {0.0f, 45.0f, 90.0f, 180.0f, 270.0f};
    for (float a : angles)
    {
        SetCell(tilemap, 0, 0, 0, {1, a, false, false});
        ClipboardRegion region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 1, 1);
        ReflectClipboardRegion(region, /*flipXAxis=*/true);

        float expected = std::fmod(360.0f - a, 360.0f);
        if (expected < 0.0f)
            expected += 360.0f;
        EXPECT_NEAR(region.cells[0].layers[0].rotation, expected, 1e-4f) << "angle " << a;
    }
}

TEST_F(TilemapFlipTest, ReflectIsInvolution_X)
{
    SetCell(tilemap, 0, 0, 0, {1, 90.0f, true, false});
    SetCell(tilemap, 1, 0, 0, {2, 0.0f, false, true});
    SetCell(tilemap, 2, 0, 0, {3, 270.0f, false, false});

    ClipboardRegion original = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 3, 1);
    ClipboardRegion twice = original;
    ReflectClipboardRegion(twice, true);
    ReflectClipboardRegion(twice, true);

    ASSERT_EQ(twice.cells.size(), original.cells.size());
    for (std::size_t i = 0; i < original.cells.size(); ++i)
    {
        EXPECT_EQ(twice.cells[i].layers[0].tileId, original.cells[i].layers[0].tileId);
        EXPECT_FLOAT_EQ(twice.cells[i].layers[0].rotation, original.cells[i].layers[0].rotation);
        EXPECT_EQ(twice.cells[i].layers[0].flipX, original.cells[i].layers[0].flipX);
        EXPECT_EQ(twice.cells[i].layers[0].flipY, original.cells[i].layers[0].flipY);
    }
}

TEST_F(TilemapFlipTest, ReflectIsInvolution_Y)
{
    SetCell(tilemap, 0, 0, 0, {1, 90.0f, true, false});
    SetCell(tilemap, 0, 1, 0, {2, 180.0f, false, true});

    ClipboardRegion original = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 1, 2);
    ClipboardRegion twice = original;
    ReflectClipboardRegion(twice, false);
    ReflectClipboardRegion(twice, false);

    for (std::size_t i = 0; i < original.cells.size(); ++i)
    {
        EXPECT_EQ(twice.cells[i].layers[0].tileId, original.cells[i].layers[0].tileId);
        EXPECT_FLOAT_EQ(twice.cells[i].layers[0].rotation, original.cells[i].layers[0].rotation);
        EXPECT_EQ(twice.cells[i].layers[0].flipX, original.cells[i].layers[0].flipX);
        EXPECT_EQ(twice.cells[i].layers[0].flipY, original.cells[i].layers[0].flipY);
    }
}

TEST_F(TilemapFlipTest, ReflectEmpty_NoChange)
{
    ClipboardRegion empty;
    ReflectClipboardRegion(empty, true);   // no crash
    ReflectClipboardRegion(empty, false);  // no crash
    EXPECT_TRUE(empty.Empty());
}

// --- End-to-end: snapshot -> reflect -> paste round-trip via undo -----------

TEST_F(TilemapFlipTest, RegionReflectViaPaste_UndoRestoresOriginal)
{
    SetCell(tilemap, 2, 2, 0, {10, 0.0f, false, false});
    SetCell(tilemap, 3, 2, 0, {11, 90.0f, false, false});

    ClipboardRegion region = PasteRegionCmd::SnapshotRegion(tilemap, 2, 2, 2, 1);
    ReflectClipboardRegion(region, /*flipXAxis=*/true);

    PasteRegionCmd paste{2, 2, region};
    paste.Apply(tilemap, npcs);

    // After reflect-X then paste:
    //  (2,2) was 10, now 11 with rotation 270 and flipX=true
    //  (3,2) was 11, now 10 with rotation 0 and flipX=true
    auto a = GetCell(tilemap, 2, 2, 0);
    auto b = GetCell(tilemap, 3, 2, 0);
    EXPECT_EQ(a.tileId, 11);
    EXPECT_FLOAT_EQ(a.rotation, 270.0f);
    EXPECT_TRUE(a.flipX);
    EXPECT_EQ(b.tileId, 10);
    EXPECT_FLOAT_EQ(b.rotation, 0.0f);
    EXPECT_TRUE(b.flipX);

    paste.Revert(tilemap, npcs);
    auto a2 = GetCell(tilemap, 2, 2, 0);
    auto b2 = GetCell(tilemap, 3, 2, 0);
    EXPECT_EQ(a2.tileId, 10);
    EXPECT_FLOAT_EQ(a2.rotation, 0.0f);
    EXPECT_FALSE(a2.flipX);
    EXPECT_EQ(b2.tileId, 11);
    EXPECT_FLOAT_EQ(b2.rotation, 90.0f);
    EXPECT_FALSE(b2.flipX);
}
