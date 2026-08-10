// Round-trip and migration guard for the per-tile TileStance field.
//
// Before this file the map format had NO test coverage at all: nothing under
// tests/ touched SaveMapToJSON or LoadMapFromJSON, so a save/load regression
// would have shipped silently. `stance` is also the format's first per-layer
// enum, and it carries a one-time migration for maps authored before it
// existed, so both halves are pinned here.
//
// No GL/Vulkan context is created (see the rift_tests constraint in
// CMakeLists.txt) - serialization is pure data.
#include "../src/Tilemap.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
constexpr size_t kGround = 0;
constexpr size_t kGroundDetail = 1;
constexpr size_t kObjects = 2;

// A unique path per test, so a failure leaves its artifact behind for
// inspection without the next test tripping over it.
std::string TempMapPath(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rift_tests";
    std::filesystem::create_directories(dir);
    return (dir / (name + ".json")).string();
}

std::string ReadWholeFile(const std::string& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteWholeFile(const std::string& path, const std::string& contents)
{
    std::ofstream out(path);
    out << contents;
}

// A legacy map: three layers, no "stance" key anywhere, flags in the old
// encoding. 4x4 tiles, so flat index = y * 4 + x.
//
// Layer 0 "Ground"        : index 0 noProjection
// Layer 1 "Ground Detail" : index 5 ySortPlus          (a flat decal)
// Layer 2 "Objects"       : index 3 ySortPlus          (an isolated prop)
//                           indices 12,13 ySortPlus    (two neighbours: a fence)
//
// Index 3 is (3,0) and the fence is (0,3),(1,3) - deliberately not 4-connected
// to it. Picking a cell in the same COLUMN as the fence would make the "isolated"
// prop a neighbour of it and the split untested.
std::string LegacyMapJson()
{
    return R"({
      "width": 4,
      "height": 4,
      "tileWidth": 16,
      "tileHeight": 16,
      "dynamicLayers": [
        {
          "name": "Ground", "renderOrder": 0, "isBackground": true,
          "tiles": { "0": 7 },
          "noProjection": [0],
          "ySortPlus": [], "ySortMinus": []
        },
        {
          "name": "Ground Detail", "renderOrder": 10, "isBackground": true,
          "tiles": { "5": 7 },
          "noProjection": [],
          "ySortPlus": [5], "ySortMinus": []
        },
        {
          "name": "Objects", "renderOrder": 20, "isBackground": true,
          "tiles": { "3": 7, "12": 7, "13": 7 },
          "noProjection": [],
          "ySortPlus": [3, 12, 13], "ySortMinus": []
        }
      ]
    })";
}
}  // namespace

TEST(TilemapSerializationTest, EveryStanceSurvivesARoundTrip)
{
    Tilemap saved;
    saved.SetTilemapSize(8, 8, false);
    saved.SetLayerTile(1, 1, kGround, 3);
    saved.SetLayerTile(2, 1, kGround, 3);
    saved.SetLayerTile(3, 1, kGround, 3);
    saved.SetLayerTile(4, 1, kGround, 3);
    saved.SetLayerStance(1, 1, kGround, TileStance::Flat);
    saved.SetLayerStance(2, 1, kGround, TileStance::Prop);
    saved.SetLayerStance(3, 1, kGround, TileStance::Wall);
    saved.SetLayerStance(4, 1, kGround, TileStance::Structure);

    const std::string path = TempMapPath("stance_roundtrip");
    ASSERT_TRUE(saved.SaveMapToJSON(path));

    Tilemap loaded;
    ASSERT_TRUE(loaded.LoadMapFromJSON(path));

    EXPECT_EQ(loaded.GetLayerStance(1, 1, kGround), TileStance::Flat);
    EXPECT_EQ(loaded.GetLayerStance(2, 1, kGround), TileStance::Prop);
    EXPECT_EQ(loaded.GetLayerStance(3, 1, kGround), TileStance::Wall);
    EXPECT_EQ(loaded.GetLayerStance(4, 1, kGround), TileStance::Structure);
}

TEST(TilemapSerializationTest, StanceIsSparseAndTheLegacyKeyIsNeverWritten)
{
    // Flat cells must not be written - a 125x125 map would otherwise gain 15625
    // entries per layer - and the field this replaced must not reappear in the
    // output, or an older build would silently read a contradictory map.
    Tilemap tm;
    tm.SetTilemapSize(8, 8, false);
    tm.SetLayerTile(2, 2, kGround, 3);
    tm.SetLayerStance(2, 2, kGround, TileStance::Wall);

    const std::string path = TempMapPath("stance_sparse");
    ASSERT_TRUE(tm.SaveMapToJSON(path));
    const std::string text = ReadWholeFile(path);

    EXPECT_EQ(text.find("noProjection\""), std::string::npos)
        << "the legacy per-tile key was written again";

    // Exactly one stance entry: flat index 2 * 8 + 2 = 18, value Wall (2).
    const size_t stanceKey = text.find("\"stance\"");
    ASSERT_NE(stanceKey, std::string::npos) << "the stance key was not written at all";
    const size_t closing = text.find('}', stanceKey);
    ASSERT_NE(closing, std::string::npos);
    const std::string block = text.substr(stanceKey, closing - stanceKey);
    EXPECT_NE(block.find("\"18\""), std::string::npos) << block;
    EXPECT_EQ(block.find("\"0\""), std::string::npos) << "a Flat cell was written: " << block;
}

TEST(TilemapSerializationTest, AnAllFlatLayerOmitsTheStanceKeyEntirely)
{
    Tilemap tm;
    tm.SetTilemapSize(4, 4, false);
    tm.SetLayerTile(1, 1, kGround, 3);

    const std::string path = TempMapPath("stance_omitted");
    ASSERT_TRUE(tm.SaveMapToJSON(path));
    EXPECT_EQ(ReadWholeFile(path).find("\"stance\""), std::string::npos);
}

TEST(TilemapSerializationTest, LegacyNoProjectionMigratesToStructure)
{
    // The one exact equivalence in the migration: noProjection has always meant
    // "this artwork stands up as part of a structure", so the flat pipeline's
    // behaviour is unchanged by construction.
    const std::string path = TempMapPath("legacy_noprojection");
    WriteWholeFile(path, LegacyMapJson());

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));
    EXPECT_EQ(tm.GetLayerStance(0, 0, kGround), TileStance::Structure);
}

TEST(TilemapSerializationTest, LegacyYSortOnAGroundLayerStaysFlat)
{
    // The bug that motivated the whole change: 168 Ground Detail cells carried a
    // y-sort flag and were being stood on their edge in 3D. They are decals.
    const std::string path = TempMapPath("legacy_grounddetail");
    WriteWholeFile(path, LegacyMapJson());

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));
    EXPECT_EQ(tm.GetLayerStance(1, 1, kGroundDetail), TileStance::Flat);
}

TEST(TilemapSerializationTest, LegacyYSortOnAnObjectLayerSplitsByAdjacency)
{
    // An isolated y-sort cell on an object layer is a lone prop; a run of them is
    // a fence. The adjacency read here is a ONE-OFF seed of a field that did not
    // exist, not a rule - nothing at render time may consult a neighbour.
    const std::string path = TempMapPath("legacy_objects");
    WriteWholeFile(path, LegacyMapJson());

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));

    // Index 3 = (3, 0), alone.
    EXPECT_EQ(tm.GetLayerStance(3, 0, kObjects), TileStance::Prop);
    // Indices 12 and 13 = (0, 3) and (1, 3), touching.
    EXPECT_EQ(tm.GetLayerStance(0, 3, kObjects), TileStance::Wall);
    EXPECT_EQ(tm.GetLayerStance(1, 3, kObjects), TileStance::Wall);
}

TEST(TilemapSerializationTest, APresentStanceKeyWinsOverAStaleLegacyKey)
{
    // A map written by a newer build and then touched by an older one could carry
    // both. The authored field must win outright rather than the two fighting.
    const std::string path = TempMapPath("stance_beats_legacy");
    WriteWholeFile(path, R"({
      "width": 4, "height": 4, "tileWidth": 16, "tileHeight": 16,
      "dynamicLayers": [
        {
          "name": "Ground", "renderOrder": 0, "isBackground": true,
          "tiles": { "0": 7 },
          "noProjection": [0],
          "stance": { "0": 1 }
        }
      ]
    })");

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));
    EXPECT_EQ(tm.GetLayerStance(0, 0, kGround), TileStance::Prop)
        << "the stale noProjection key overrode the authored stance";
}

TEST(TilemapSerializationTest, AnOutOfRangeStanceValueIsIgnoredNotCastBlindly)
{
    // The value is cast to an enum, so an out-of-range integer would otherwise
    // produce a TileStance nothing in the engine handles.
    const std::string path = TempMapPath("stance_out_of_range");
    WriteWholeFile(path, R"({
      "width": 4, "height": 4, "tileWidth": 16, "tileHeight": 16,
      "dynamicLayers": [
        {
          "name": "Ground", "renderOrder": 0, "isBackground": true,
          "tiles": { "0": 7, "1": 7 },
          "stance": { "0": 99, "1": 2 }
        }
      ]
    })");

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));
    EXPECT_EQ(tm.GetLayerStance(0, 0, kGround), TileStance::Flat);
    EXPECT_EQ(tm.GetLayerStance(1, 0, kGround), TileStance::Wall);
}

TEST(TilemapSerializationTest, TheSiblingPerTileFieldsStillRoundTrip)
{
    // Guards the five-place checklist that adding `stance` to TileLayer had to
    // satisfy: a field missing from the resize_all fold is silently swallowed by
    // the bounds guards, and a neighbour dropped from the save/load loops fails
    // just as quietly. Cheap insurance on fields that had no coverage either.
    Tilemap saved;
    saved.SetTilemapSize(8, 8, false);
    saved.SetLayerTile(3, 4, kObjects, 11);
    saved.SetLayerRotation(3, 4, kObjects, 90.0f);
    saved.SetLayerFlipX(3, 4, kObjects, true);
    saved.SetLayerYSortPlus(3, 4, kObjects, true);
    saved.SetLayerYSortMinus(3, 4, kObjects, true);
    saved.SetLayerStance(3, 4, kObjects, TileStance::Prop);

    const std::string path = TempMapPath("sibling_fields");
    ASSERT_TRUE(saved.SaveMapToJSON(path));

    Tilemap loaded;
    ASSERT_TRUE(loaded.LoadMapFromJSON(path));

    EXPECT_EQ(loaded.GetLayerTile(3, 4, kObjects), 11);
    EXPECT_FLOAT_EQ(loaded.GetLayerRotation(3, 4, kObjects), 90.0f);
    EXPECT_TRUE(loaded.GetLayerFlipX(3, 4, kObjects));
    EXPECT_FALSE(loaded.GetLayerFlipY(3, 4, kObjects));
    EXPECT_TRUE(loaded.GetLayerYSortPlus(3, 4, kObjects));
    EXPECT_TRUE(loaded.GetLayerYSortMinus(3, 4, kObjects));
    EXPECT_EQ(loaded.GetLayerStance(3, 4, kObjects), TileStance::Prop);
}

TEST(TilemapSerializationTest, EveryElevationRoleSurvivesARoundTrip)
{
    Tilemap saved;
    saved.SetTilemapSize(8, 8, false);
    saved.SetLayerTile(1, 1, kGround, 3);
    saved.SetLayerTile(2, 1, kGround, 3);
    saved.SetLayerTile(3, 1, kGround, 3);
    saved.SetLayerElevationRole(1, 1, kGround, ElevationRole::Ground);
    saved.SetLayerElevationRole(2, 1, kGround, ElevationRole::Raised);
    saved.SetLayerElevationRole(3, 1, kGround, ElevationRole::Ramp);

    const std::string path = TempMapPath("elevation_role_roundtrip");
    ASSERT_TRUE(saved.SaveMapToJSON(path));

    Tilemap loaded;
    ASSERT_TRUE(loaded.LoadMapFromJSON(path));

    EXPECT_EQ(loaded.GetLayerElevationRole(1, 1, kGround), ElevationRole::Ground);
    EXPECT_EQ(loaded.GetLayerElevationRole(2, 1, kGround), ElevationRole::Raised);
    EXPECT_EQ(loaded.GetLayerElevationRole(3, 1, kGround), ElevationRole::Ramp);
}

TEST(TilemapSerializationTest, ElevationRoleIsPerLayerThroughARoundTrip)
{
    // The storage decision has to survive the file, not just memory: one cell, one
    // elevation, two layers disagreeing about whether they rise to it.
    Tilemap saved;
    saved.SetTilemapSize(8, 8, false);
    saved.SetElevation(4, 4, 6);
    saved.SetLayerTile(4, 4, kGround, 3);
    saved.SetLayerTile(4, 4, kObjects, 3);
    saved.SetLayerElevationRole(4, 4, kObjects, ElevationRole::Raised);

    const std::string path = TempMapPath("elevation_role_per_layer");
    ASSERT_TRUE(saved.SaveMapToJSON(path));

    Tilemap loaded;
    ASSERT_TRUE(loaded.LoadMapFromJSON(path));

    EXPECT_EQ(loaded.GetElevation(4, 4), 6);
    EXPECT_EQ(loaded.GetLayerElevationRole(4, 4, kGround), ElevationRole::Ground);
    EXPECT_EQ(loaded.GetLayerElevationRole(4, 4, kObjects), ElevationRole::Raised);
}

TEST(TilemapSerializationTest, AnAllGroundLayerOmitsTheElevationRoleKey)
{
    // Elevation alone must not write the role key - a 125x125 map would otherwise
    // gain 15625 entries per layer for a field nobody set.
    Tilemap tm;
    tm.SetTilemapSize(4, 4, false);
    tm.SetLayerTile(1, 1, kGround, 3);
    tm.SetElevation(1, 1, 6);

    const std::string path = TempMapPath("elevation_role_omitted");
    ASSERT_TRUE(tm.SaveMapToJSON(path));
    EXPECT_EQ(ReadWholeFile(path).find("\"elevationRole\""), std::string::npos);
}

TEST(TilemapSerializationTest, ElevationRoleIsSparse)
{
    // Only non-Ground cells are written.
    Tilemap tm;
    tm.SetTilemapSize(8, 8, false);
    tm.SetLayerTile(2, 2, kGround, 3);
    tm.SetLayerElevationRole(2, 2, kGround, ElevationRole::Ramp);

    const std::string path = TempMapPath("elevation_role_sparse");
    ASSERT_TRUE(tm.SaveMapToJSON(path));
    const std::string text = ReadWholeFile(path);

    const size_t key = text.find("\"elevationRole\"");
    ASSERT_NE(key, std::string::npos);
    const size_t closing = text.find('}', key);
    ASSERT_NE(closing, std::string::npos);
    const std::string block = text.substr(key, closing - key);

    // Flat index 2 * 8 + 2 = 18, value Ramp (2).
    EXPECT_NE(block.find("\"18\""), std::string::npos) << block;
    EXPECT_EQ(block.find("\"0\""), std::string::npos) << "a Ground cell was written: " << block;
}

TEST(TilemapSerializationTest, AMapWithNoElevationRoleKeyLoadsAsAllGround)
{
    // No migration by design: the absence of the key IS the correct answer, so an
    // old map renders exactly as it did until something is marked.
    const std::string path = TempMapPath("elevation_role_absent");
    WriteWholeFile(path, R"({
      "width": 4, "height": 4, "tileWidth": 16, "tileHeight": 16,
      "elevation": { "5": 6 },
      "dynamicLayers": [
        {
          "name": "Ground", "renderOrder": 0, "isBackground": true,
          "tiles": { "5": 7 }
        }
      ]
    })");

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));
    EXPECT_EQ(tm.GetElevation(1, 1), 6);
    EXPECT_EQ(tm.GetLayerElevationRole(1, 1, kGround), ElevationRole::Ground);
}

TEST(TilemapSerializationTest, AnOutOfRangeElevationRoleIsIgnoredNotCastBlindly)
{
    // The value is cast to an enum, so an out-of-range integer would otherwise
    // produce an ElevationRole nothing in the engine handles.
    const std::string path = TempMapPath("elevation_role_out_of_range");
    WriteWholeFile(path, R"({
      "width": 4, "height": 4, "tileWidth": 16, "tileHeight": 16,
      "dynamicLayers": [
        {
          "name": "Ground", "renderOrder": 0, "isBackground": true,
          "tiles": { "0": 7, "1": 7 },
          "elevationRole": { "0": 42, "1": 2 }
        }
      ]
    })");

    Tilemap tm;
    ASSERT_TRUE(tm.LoadMapFromJSON(path));
    EXPECT_EQ(tm.GetLayerElevationRole(0, 0, kGround), ElevationRole::Ground);
    EXPECT_EQ(tm.GetLayerElevationRole(1, 0, kGround), ElevationRole::Ramp);
}
