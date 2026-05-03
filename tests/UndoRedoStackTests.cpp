// Tests for UndoRedoStack and the PlaceTilesCmd command.
//
// The stack is exercised with a StubCmd that mutates a captured int counter so
// we can verify Apply / Revert / Push semantics without a Tilemap. PlaceTilesCmd
// is exercised against a real (data-only) Tilemap. Per the test infra contract,
// no GL or Vulkan context is created here.

#include <gtest/gtest.h>

#include "../src/EditorCommand.h"
#include "../src/EditorCommands.h"
#include "../src/EditorStrokeAccumulators.h"
#include "../src/NonPlayerCharacter.h"
#include "../src/Tilemap.h"
#include "../src/UndoRedoStack.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
// Minimal command for stack-mechanics tests. Adds m_Delta to *m_Counter on
// Apply, subtracts on Revert. Lets us test push / pop / capacity / redo-clear
// without touching Tilemap or NPC code.
class StubCmd : public EditorCommand
{
public:
    StubCmd(int* counter, int delta) : m_Counter(counter), m_Delta(delta) {}

    void Apply(Tilemap&, std::vector<NonPlayerCharacter>&) override
    {
        *m_Counter += m_Delta;
    }
    void Revert(Tilemap&, std::vector<NonPlayerCharacter>&) override
    {
        *m_Counter -= m_Delta;
    }
    [[nodiscard]] std::string DebugLabel() const override { return "Stub"; }

private:
    int* m_Counter;
    int m_Delta;
};
}  // namespace

// --- Stack mechanics ---------------------------------------------------------

class UndoRedoStackTest : public ::testing::Test
{
protected:
    UndoRedoStack stack;
    Tilemap tilemap;  // unused by StubCmd but required by the API
    std::vector<NonPlayerCharacter> npcs;
    int counter = 0;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }

    std::unique_ptr<StubCmd> Stub(int delta)
    {
        return std::make_unique<StubCmd>(&counter, delta);
    }
};

TEST_F(UndoRedoStackTest, Empty_NoUndoOrRedo)
{
    EXPECT_FALSE(stack.CanUndo());
    EXPECT_FALSE(stack.CanRedo());
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(stack.UndoLabel(), "");
    EXPECT_EQ(stack.RedoLabel(), "");
}

TEST_F(UndoRedoStackTest, Execute_AppliesAndPushes)
{
    stack.Execute(Stub(5), tilemap, npcs);
    EXPECT_EQ(counter, 5);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_FALSE(stack.CanRedo());
    EXPECT_EQ(stack.UndoLabel(), "Stub");
}

TEST_F(UndoRedoStackTest, Undo_RevertsAndMovesToRedo)
{
    stack.Execute(Stub(3), tilemap, npcs);
    EXPECT_TRUE(stack.Undo(tilemap, npcs));
    EXPECT_EQ(counter, 0);
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 1u);
}

TEST_F(UndoRedoStackTest, Undo_OnEmpty_ReturnsFalse)
{
    EXPECT_FALSE(stack.Undo(tilemap, npcs));
    EXPECT_EQ(counter, 0);
}

TEST_F(UndoRedoStackTest, Redo_OnEmpty_ReturnsFalse)
{
    EXPECT_FALSE(stack.Redo(tilemap, npcs));
}

TEST_F(UndoRedoStackTest, Redo_ReappliesAndMovesBack)
{
    stack.Execute(Stub(7), tilemap, npcs);
    stack.Undo(tilemap, npcs);
    EXPECT_TRUE(stack.Redo(tilemap, npcs));
    EXPECT_EQ(counter, 7);
    EXPECT_EQ(stack.UndoSize(), 1u);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST_F(UndoRedoStackTest, NewExecuteAfterUndo_ClearsRedo)
{
    stack.Execute(Stub(2), tilemap, npcs);
    stack.Undo(tilemap, npcs);
    ASSERT_EQ(stack.RedoSize(), 1u);
    stack.Execute(Stub(1), tilemap, npcs);
    EXPECT_EQ(stack.RedoSize(), 0u);
    EXPECT_EQ(stack.UndoSize(), 1u);
}

TEST_F(UndoRedoStackTest, Capacity_OldestEvicted)
{
    UndoRedoStack small{3};
    for (int i = 0; i < 5; ++i)
        small.Execute(Stub(1), tilemap, npcs);
    EXPECT_EQ(small.UndoSize(), 3u);  // oldest 2 dropped
    EXPECT_EQ(counter, 5);            // every Apply still ran
}

TEST_F(UndoRedoStackTest, DefaultCapacity_Is100)
{
    EXPECT_EQ(stack.Capacity(), UndoRedoStack::DEFAULT_CAPACITY);
    EXPECT_EQ(UndoRedoStack::DEFAULT_CAPACITY, 100u);
}

TEST_F(UndoRedoStackTest, Clear_RemovesAll)
{
    stack.Execute(Stub(1), tilemap, npcs);
    stack.Execute(Stub(1), tilemap, npcs);
    stack.Undo(tilemap, npcs);
    ASSERT_EQ(stack.UndoSize(), 1u);
    ASSERT_EQ(stack.RedoSize(), 1u);
    stack.Clear();
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST_F(UndoRedoStackTest, Push_DoesNotApplyButAddsToUndo)
{
    // Stroke-commit semantics: caller already mutated state during the drag,
    // so Push must not re-Apply (would double-mutate).
    counter = 10;  // simulate already-applied state
    stack.Push(Stub(10));
    EXPECT_EQ(counter, 10);  // not re-applied
    EXPECT_EQ(stack.UndoSize(), 1u);

    // But Undo should still revert it normally.
    stack.Undo(tilemap, npcs);
    EXPECT_EQ(counter, 0);
}

TEST_F(UndoRedoStackTest, Push_ClearsRedo)
{
    stack.Execute(Stub(1), tilemap, npcs);
    stack.Undo(tilemap, npcs);
    ASSERT_EQ(stack.RedoSize(), 1u);
    counter = 5;
    stack.Push(Stub(5));
    EXPECT_EQ(stack.RedoSize(), 0u);
}

TEST_F(UndoRedoStackTest, ExecuteNullptr_NoOp)
{
    stack.Execute(nullptr, tilemap, npcs);
    EXPECT_EQ(stack.UndoSize(), 0u);
    EXPECT_EQ(counter, 0);
}

TEST_F(UndoRedoStackTest, Execute_Undo_Redo_Sequence_PreservesOrder)
{
    stack.Execute(Stub(1), tilemap, npcs);
    stack.Execute(Stub(10), tilemap, npcs);
    stack.Execute(Stub(100), tilemap, npcs);
    EXPECT_EQ(counter, 111);

    stack.Undo(tilemap, npcs);  // -100
    EXPECT_EQ(counter, 11);
    stack.Undo(tilemap, npcs);  // -10
    EXPECT_EQ(counter, 1);
    stack.Undo(tilemap, npcs);  // -1
    EXPECT_EQ(counter, 0);
    EXPECT_FALSE(stack.CanUndo());

    stack.Redo(tilemap, npcs);  // +1
    stack.Redo(tilemap, npcs);  // +10
    stack.Redo(tilemap, npcs);  // +100
    EXPECT_EQ(counter, 111);
    EXPECT_FALSE(stack.CanRedo());
}

// --- PlaceTilesCmd round-trip ------------------------------------------------

class PlaceTilesCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(PlaceTilesCmdTest, Apply_SetsTileAndRotation)
{
    PlaceTilesCmd cmd{std::vector<PlaceTilesCmd::Entry>{
        {3, 4, 0, /*oldId=*/-1, /*oldRot=*/0.0f, /*newId=*/42, /*newRot=*/90.0f}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(3, 4, 0), 42);
    EXPECT_FLOAT_EQ(tilemap.GetLayerRotation(3, 4, 0), 90.0f);
}

TEST_F(PlaceTilesCmdTest, Revert_RestoresPreviousTileAndRotation)
{
    tilemap.SetLayerTile(2, 2, 1, 5);
    tilemap.SetLayerRotation(2, 2, 1, 180.0f);

    PlaceTilesCmd cmd{std::vector<PlaceTilesCmd::Entry>{{2, 2, 1, 5, 180.0f, 7, 0.0f}}};
    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(tilemap.GetLayerTile(2, 2, 1), 7);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(2, 2, 1), 5);
    EXPECT_FLOAT_EQ(tilemap.GetLayerRotation(2, 2, 1), 180.0f);
}

TEST_F(PlaceTilesCmdTest, MultiTile_ApplyRevert_RestoresAllToDefault)
{
    std::vector<PlaceTilesCmd::Entry> entries;
    for (int i = 0; i < 5; ++i)
        entries.push_back({i, 0, 0, /*oldId=*/-1, 0.0f, i + 100, 0.0f});

    PlaceTilesCmd cmd{std::move(entries)};
    cmd.Apply(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(tilemap.GetLayerTile(i, 0, 0), i + 100);

    cmd.Revert(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(tilemap.GetLayerTile(i, 0, 0), -1);  // defaulted_vector default
}

TEST_F(PlaceTilesCmdTest, ApplyRevertApply_Idempotent)
{
    PlaceTilesCmd cmd{std::vector<PlaceTilesCmd::Entry>{{1, 1, 0, -1, 0.0f, 99, 45.0f}}};
    cmd.Apply(tilemap, npcs);
    cmd.Revert(tilemap, npcs);
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(1, 1, 0), 99);
    EXPECT_FLOAT_EQ(tilemap.GetLayerRotation(1, 1, 0), 45.0f);
}

TEST_F(PlaceTilesCmdTest, DebugLabel_ReportsCount)
{
    PlaceTilesCmd one{std::vector<PlaceTilesCmd::Entry>{{0, 0, 0, -1, 0.0f, 1, 0.0f}}};
    EXPECT_EQ(one.DebugLabel(), "Place 1 tile(s)");

    std::vector<PlaceTilesCmd::Entry> manyEntries(3, PlaceTilesCmd::Entry{0, 0, 0, -1, 0.0f, 1, 0.0f});
    PlaceTilesCmd many{std::move(manyEntries)};
    EXPECT_EQ(many.DebugLabel(), "Place 3 tile(s)");
}

TEST_F(PlaceTilesCmdTest, StackIntegration_RoundTrip)
{
    UndoRedoStack stack;

    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), -1);

    auto cmd = std::make_unique<PlaceTilesCmd>(
        std::vector<PlaceTilesCmd::Entry>{{0, 0, 0, -1, 0.0f, 50, 0.0f}});
    stack.Execute(std::move(cmd), tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), 50);

    stack.Undo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), -1);

    stack.Redo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), 50);
}

TEST_F(PlaceTilesCmdTest, AcrossMultipleLayers_EachLayerTracked)
{
    PlaceTilesCmd cmd{std::vector<PlaceTilesCmd::Entry>{
        {0, 0, 0, -1, 0.0f, 1, 0.0f},
        {0, 0, 1, -1, 0.0f, 2, 0.0f},
        {0, 0, 2, -1, 0.0f, 3, 0.0f}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), 1);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 1), 2);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 2), 3);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), -1);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 1), -1);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 2), -1);
}

// --- CollisionToggleCmd ------------------------------------------------------

class CollisionToggleCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(CollisionToggleCmdTest, Apply_TogglesCollision)
{
    CollisionToggleCmd cmd{
        std::vector<CollisionToggleCmd::Entry>{{4, 4, /*old=*/false, /*new=*/true}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetTileCollision(4, 4));
}

TEST_F(CollisionToggleCmdTest, Revert_RestoresOriginal)
{
    tilemap.SetTileCollision(2, 3, true);
    CollisionToggleCmd cmd{
        std::vector<CollisionToggleCmd::Entry>{{2, 3, /*old=*/true, /*new=*/false}}};
    cmd.Apply(tilemap, npcs);
    ASSERT_FALSE(tilemap.GetTileCollision(2, 3));
    cmd.Revert(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetTileCollision(2, 3));
}

TEST_F(CollisionToggleCmdTest, MultiTile_RoundTrip)
{
    std::vector<CollisionToggleCmd::Entry> entries;
    for (int i = 0; i < 5; ++i)
        entries.push_back({i, 0, false, true});
    CollisionToggleCmd cmd{std::move(entries)};
    cmd.Apply(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(tilemap.GetTileCollision(i, 0));
    cmd.Revert(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_FALSE(tilemap.GetTileCollision(i, 0));
}

// --- ElevationSetCmd ---------------------------------------------------------

class ElevationSetCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(ElevationSetCmdTest, Apply_SetsElevation)
{
    ElevationSetCmd cmd{
        std::vector<ElevationSetCmd::Entry>{{1, 1, /*old=*/0, /*new=*/12}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetElevation(1, 1), 12);
}

TEST_F(ElevationSetCmdTest, Revert_RestoresOriginal)
{
    tilemap.SetElevation(2, 2, 8);
    ElevationSetCmd cmd{
        std::vector<ElevationSetCmd::Entry>{{2, 2, 8, -4}}};
    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(tilemap.GetElevation(2, 2), -4);
    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetElevation(2, 2), 8);
}

TEST_F(ElevationSetCmdTest, ApplyRevertApply_Idempotent)
{
    ElevationSetCmd cmd{
        std::vector<ElevationSetCmd::Entry>{{3, 3, 0, 16}}};
    cmd.Apply(tilemap, npcs);
    cmd.Revert(tilemap, npcs);
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetElevation(3, 3), 16);
}

// --- PlaceNPCCmd / RemoveNPCCmd ---------------------------------------------

namespace
{
// Build a default-constructed NPC at given coords. Skip Load() to avoid
// touching texture I/O - tests run without a GL/Vulkan context.
NonPlayerCharacter MakeStubNPC(int tileX, int tileY)
{
    NonPlayerCharacter npc;
    npc.SetTilePosition(tileX, tileY, 16);
    return npc;
}
}  // namespace

class NPCCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(NPCCmdTest, PlaceNPCCmd_Apply_AddsToVector)
{
    PlaceNPCCmd cmd{MakeStubNPC(3, 4)};
    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(npcs.size(), 1u);
    EXPECT_EQ(npcs[0].GetTileX(), 3);
    EXPECT_EQ(npcs[0].GetTileY(), 4);
}

TEST_F(NPCCmdTest, PlaceNPCCmd_Revert_RemovesFromVector)
{
    PlaceNPCCmd cmd{MakeStubNPC(5, 6)};
    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(npcs.size(), 1u);
    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);
}

TEST_F(NPCCmdTest, PlaceNPCCmd_RoundTrip_PreservesIdentity)
{
    PlaceNPCCmd cmd{MakeStubNPC(2, 3)};
    cmd.Apply(tilemap, npcs);
    cmd.Revert(tilemap, npcs);
    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(npcs.size(), 1u);
    EXPECT_EQ(npcs[0].GetTileX(), 2);
    EXPECT_EQ(npcs[0].GetTileY(), 3);
}

TEST_F(NPCCmdTest, RemoveNPCCmd_Apply_RemovesNPCAtTile)
{
    npcs.push_back(MakeStubNPC(7, 7));
    ASSERT_EQ(npcs.size(), 1u);
    RemoveNPCCmd cmd{7, 7};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);
}

TEST_F(NPCCmdTest, RemoveNPCCmd_Revert_ReinsertsNPC)
{
    npcs.push_back(MakeStubNPC(7, 7));
    RemoveNPCCmd cmd{7, 7};
    cmd.Apply(tilemap, npcs);
    cmd.Revert(tilemap, npcs);
    ASSERT_EQ(npcs.size(), 1u);
    EXPECT_EQ(npcs[0].GetTileX(), 7);
    EXPECT_EQ(npcs[0].GetTileY(), 7);
}

TEST_F(NPCCmdTest, PlaceThenRemove_StackUndoRedoSequence)
{
    UndoRedoStack stack;
    stack.Execute(std::make_unique<PlaceNPCCmd>(MakeStubNPC(1, 1)), tilemap, npcs);
    EXPECT_EQ(npcs.size(), 1u);
    stack.Execute(std::make_unique<RemoveNPCCmd>(1, 1), tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);

    stack.Undo(tilemap, npcs);  // un-removes
    EXPECT_EQ(npcs.size(), 1u);
    stack.Undo(tilemap, npcs);  // un-places
    EXPECT_EQ(npcs.size(), 0u);

    stack.Redo(tilemap, npcs);  // re-places
    EXPECT_EQ(npcs.size(), 1u);
    stack.Redo(tilemap, npcs);  // re-removes
    EXPECT_EQ(npcs.size(), 0u);
}

TEST_F(NPCCmdTest, RemoveNPCCmd_NoNPCAtTile_NoOpApply)
{
    RemoveNPCCmd cmd{0, 0};
    cmd.Apply(tilemap, npcs);  // npcs empty, should not crash
    EXPECT_EQ(npcs.size(), 0u);
    cmd.Revert(tilemap, npcs);  // m_Held empty, should not crash
    EXPECT_EQ(npcs.size(), 0u);
}

// --- Stroke accumulators -----------------------------------------------------

class StrokeAccumulatorTest : public ::testing::Test
{
protected:
    UndoRedoStack stack;
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(StrokeAccumulatorTest, TilePlace_FiveTiles_RevertedAtomically)
{
    TilePlaceStrokeAccum accum;
    accum.Begin();
    for (int i = 0; i < 5; ++i)
    {
        // Simulate the editor: capture old, mutate, register touch.
        int oldId = tilemap.GetLayerTile(i, 0, 0);
        float oldRot = tilemap.GetLayerRotation(i, 0, 0);
        tilemap.SetLayerTile(i, 0, 0, 100 + i);
        tilemap.SetLayerRotation(i, 0, 0, 90.0f);
        accum.Touch(i, 0, 0, oldId, oldRot, 100 + i, 90.0f);
    }
    accum.Commit(stack);

    ASSERT_EQ(stack.UndoSize(), 1u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(tilemap.GetLayerTile(i, 0, 0), 100 + i);

    stack.Undo(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(tilemap.GetLayerTile(i, 0, 0), -1);  // back to default
}

TEST_F(StrokeAccumulatorTest, TilePlace_RepeatedTouch_PreservesOriginalOldValue)
{
    // Pre-stroke state: tile = 2, rotation = 0
    tilemap.SetLayerTile(3, 3, 0, 2);
    tilemap.SetLayerRotation(3, 3, 0, 0.0f);

    TilePlaceStrokeAccum accum;
    accum.Begin();

    // First touch: paint over old=2 with new=5
    int oldId1 = tilemap.GetLayerTile(3, 3, 0);
    float oldRot1 = tilemap.GetLayerRotation(3, 3, 0);
    tilemap.SetLayerTile(3, 3, 0, 5);
    tilemap.SetLayerRotation(3, 3, 0, 90.0f);
    accum.Touch(3, 3, 0, oldId1, oldRot1, 5, 90.0f);

    // Second touch on same tile mid-stroke: paint over current=5 with new=8
    int oldId2 = tilemap.GetLayerTile(3, 3, 0);  // = 5 now
    float oldRot2 = tilemap.GetLayerRotation(3, 3, 0);
    tilemap.SetLayerTile(3, 3, 0, 8);
    tilemap.SetLayerRotation(3, 3, 0, 180.0f);
    accum.Touch(3, 3, 0, oldId2, oldRot2, 8, 180.0f);

    accum.Commit(stack);

    // Live state should reflect the final new value.
    EXPECT_EQ(tilemap.GetLayerTile(3, 3, 0), 8);

    // Revert must restore the ORIGINAL old (2 / 0.0f), not the intermediate 5.
    stack.Undo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(3, 3, 0), 2);
    EXPECT_FLOAT_EQ(tilemap.GetLayerRotation(3, 3, 0), 0.0f);
}

TEST_F(StrokeAccumulatorTest, TilePlace_Drop_DiscardsWithoutCommit)
{
    TilePlaceStrokeAccum accum;
    accum.Begin();
    tilemap.SetLayerTile(0, 0, 0, 99);
    accum.Touch(0, 0, 0, -1, 0.0f, 99, 0.0f);
    accum.Drop();

    EXPECT_EQ(stack.UndoSize(), 0u);  // nothing pushed
    EXPECT_FALSE(accum.IsActive());
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), 99);  // tile mutation unaffected
}

TEST_F(StrokeAccumulatorTest, TilePlace_EmptyStroke_NoCommit)
{
    TilePlaceStrokeAccum accum;
    accum.Begin();
    accum.Commit(stack);
    EXPECT_EQ(stack.UndoSize(), 0u);
}

TEST_F(StrokeAccumulatorTest, TilePlace_CommitNotActive_NoCommit)
{
    TilePlaceStrokeAccum accum;
    accum.Commit(stack);  // never Begin()
    EXPECT_EQ(stack.UndoSize(), 0u);
}

TEST_F(StrokeAccumulatorTest, Collision_RoundTripDrag)
{
    CollisionStrokeAccum accum;
    accum.Begin();
    for (int i = 0; i < 4; ++i)
    {
        bool oldHas = tilemap.GetTileCollision(i, 1);
        tilemap.SetTileCollision(i, 1, true);
        accum.Touch(i, 1, oldHas, true);
    }
    accum.Commit(stack);

    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(tilemap.GetTileCollision(i, 1));

    stack.Undo(tilemap, npcs);
    for (int i = 0; i < 4; ++i)
        EXPECT_FALSE(tilemap.GetTileCollision(i, 1));
}

TEST_F(StrokeAccumulatorTest, Elevation_RoundTripDrag)
{
    tilemap.SetElevation(0, 0, 4);
    tilemap.SetElevation(1, 0, 4);

    ElevationStrokeAccum accum;
    accum.Begin();
    for (int i = 0; i < 2; ++i)
    {
        int oldElev = tilemap.GetElevation(i, 0);
        tilemap.SetElevation(i, 0, -8);
        accum.Touch(i, 0, oldElev, -8);
    }
    accum.Commit(stack);

    EXPECT_EQ(tilemap.GetElevation(0, 0), -8);
    EXPECT_EQ(tilemap.GetElevation(1, 0), -8);

    stack.Undo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetElevation(0, 0), 4);
    EXPECT_EQ(tilemap.GetElevation(1, 0), 4);
}

TEST_F(StrokeAccumulatorTest, Multiple_LayersIndependentlyTracked)
{
    // Same (x, y) on different layers should not dedup against each other.
    TilePlaceStrokeAccum accum;
    accum.Begin();
    accum.Touch(0, 0, 0, -1, 0.0f, 1, 0.0f);
    accum.Touch(0, 0, 1, -1, 0.0f, 2, 0.0f);
    accum.Touch(0, 0, 2, -1, 0.0f, 3, 0.0f);
    tilemap.SetLayerTile(0, 0, 0, 1);
    tilemap.SetLayerTile(0, 0, 1, 2);
    tilemap.SetLayerTile(0, 0, 2, 3);
    accum.Commit(stack);

    stack.Undo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), -1);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 1), -1);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 2), -1);
}

// --- NavigationStrokeCmd ----------------------------------------------------

class NavigationStrokeCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override
    {
        tilemap.SetTilemapSize(8, 8, false);
        // Mark all tiles walkable initially so NPCs survive.
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                tilemap.SetNavigation(x, y, true);
    }
};

TEST_F(NavigationStrokeCmdTest, NoNPCs_RoundTripPreservesNav)
{
    NavigationStrokeCmd cmd{std::vector<NavigationStrokeCmd::Entry>{
        {3, 3, /*old=*/true, /*new=*/false},
        {3, 4, /*old=*/true, /*new=*/false}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_FALSE(tilemap.GetNavigation(3, 3));
    EXPECT_FALSE(tilemap.GetNavigation(3, 4));

    cmd.Revert(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetNavigation(3, 3));
    EXPECT_TRUE(tilemap.GetNavigation(3, 4));
}

TEST_F(NavigationStrokeCmdTest, NPCDisplaced_RestoredOnRevert)
{
    NonPlayerCharacter npc;
    npc.SetTilePosition(4, 4, 16);
    npcs.push_back(std::move(npc));

    NavigationStrokeCmd cmd{std::vector<NavigationStrokeCmd::Entry>{
        {4, 4, /*old=*/true, /*new=*/false}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);  // NPC erased

    cmd.Revert(tilemap, npcs);
    ASSERT_EQ(npcs.size(), 1u);
    EXPECT_EQ(npcs[0].GetTileX(), 4);
    EXPECT_EQ(npcs[0].GetTileY(), 4);
    EXPECT_TRUE(tilemap.GetNavigation(4, 4));  // nav restored
}

TEST_F(NavigationStrokeCmdTest, ApplyRevertApply_NoNPCDuplication)
{
    NonPlayerCharacter npc;
    npc.SetTilePosition(2, 2, 16);
    npcs.push_back(std::move(npc));

    NavigationStrokeCmd cmd{std::vector<NavigationStrokeCmd::Entry>{
        {2, 2, true, false}}};
    cmd.Apply(tilemap, npcs);
    cmd.Revert(tilemap, npcs);
    cmd.Apply(tilemap, npcs);  // Redo

    EXPECT_EQ(npcs.size(), 0u);  // NPC erased again on Redo, not duplicated

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 1u);  // back once
}

TEST_F(NavigationStrokeCmdTest, MultipleNPCsDisplacedTogether)
{
    for (int i = 0; i < 3; ++i)
    {
        NonPlayerCharacter npc;
        npc.SetTilePosition(i, 0, 16);
        npcs.push_back(std::move(npc));
    }

    NavigationStrokeCmd cmd{std::vector<NavigationStrokeCmd::Entry>{
        {0, 0, true, false}, {1, 0, true, false}, {2, 0, true, false}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 3u);
}

TEST_F(NavigationStrokeCmdTest, StackIntegration_UndoRedoSequence)
{
    UndoRedoStack stack;

    NonPlayerCharacter npc;
    npc.SetTilePosition(5, 5, 16);
    npcs.push_back(std::move(npc));

    auto cmd = std::make_unique<NavigationStrokeCmd>(
        std::vector<NavigationStrokeCmd::Entry>{{5, 5, true, false}});
    stack.Execute(std::move(cmd), tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);

    stack.Undo(tilemap, npcs);
    ASSERT_EQ(npcs.size(), 1u);
    EXPECT_TRUE(tilemap.GetNavigation(5, 5));

    stack.Redo(tilemap, npcs);
    EXPECT_EQ(npcs.size(), 0u);
    EXPECT_FALSE(tilemap.GetNavigation(5, 5));
}

// --- Mode flag commands (B/Y/O) ---------------------------------------------

class FlagToggleCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(FlagToggleCmdTest, NoProjection_RoundTrip)
{
    NoProjectionToggleCmd cmd{
        std::vector<LayerFlagEntry>{{2, 2, 0, /*old=*/false, /*new=*/true}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetLayerNoProjection(2, 2, 0));
    cmd.Revert(tilemap, npcs);
    EXPECT_FALSE(tilemap.GetLayerNoProjection(2, 2, 0));
}

TEST_F(FlagToggleCmdTest, YSortPlus_RoundTrip)
{
    tilemap.SetLayerYSortPlus(3, 3, 1, true);
    YSortPlusToggleCmd cmd{
        std::vector<LayerFlagEntry>{{3, 3, 1, /*old=*/true, /*new=*/false}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_FALSE(tilemap.GetLayerYSortPlus(3, 3, 1));
    cmd.Revert(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetLayerYSortPlus(3, 3, 1));
}

TEST_F(FlagToggleCmdTest, YSortMinus_RoundTrip)
{
    YSortMinusToggleCmd cmd{
        std::vector<LayerFlagEntry>{{4, 4, 2, /*old=*/false, /*new=*/true}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetLayerYSortMinus(4, 4, 2));
    cmd.Revert(tilemap, npcs);
    EXPECT_FALSE(tilemap.GetLayerYSortMinus(4, 4, 2));
}

TEST_F(FlagToggleCmdTest, MultipleEntries_AllRoundTrip)
{
    std::vector<LayerFlagEntry> entries;
    for (int i = 0; i < 5; ++i)
        entries.push_back({i, 0, 0, false, true});
    NoProjectionToggleCmd cmd{std::move(entries)};
    cmd.Apply(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(tilemap.GetLayerNoProjection(i, 0, 0));
    cmd.Revert(tilemap, npcs);
    for (int i = 0; i < 5; ++i)
        EXPECT_FALSE(tilemap.GetLayerNoProjection(i, 0, 0));
}

// --- SetTileAnimationCmd ----------------------------------------------------

class SetTileAnimationCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(SetTileAnimationCmdTest, RemoveAnimation_RestoresTile)
{
    AnimatedTile anim;
    anim.frames = {1, 2, 3};
    anim.frameDuration = 0.2f;
    int animId = tilemap.AddAnimatedTile(anim);

    tilemap.SetLayerTile(2, 2, 0, 99);
    tilemap.SetTileAnimation(2, 2, 0, animId);
    // After SetTileAnimation, tile id is now anim.frames[0] = 1.
    ASSERT_EQ(tilemap.GetLayerTile(2, 2, 0), 1);

    // Build cmd that captures the tile id BEFORE animation set the anim
    // (oldTileId = 99, the user's chosen tile pre-animation-apply).
    SetTileAnimationCmd cmd{std::vector<SetTileAnimationCmd::Entry>{
        {2, 2, 0, /*oldAnim=*/animId, /*newAnim=*/-1, /*oldTileId=*/99}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetTileAnimation(2, 2, 0), -1);

    // Revert reapplies animation AND restores oldTileId.
    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetTileAnimation(2, 2, 0), animId);
    EXPECT_EQ(tilemap.GetLayerTile(2, 2, 0), 99);
}

TEST_F(SetTileAnimationCmdTest, ApplyAnimation_RoundTrip)
{
    AnimatedTile anim;
    anim.frames = {10, 20};
    anim.frameDuration = 0.1f;
    int animId = tilemap.AddAnimatedTile(anim);

    tilemap.SetLayerTile(0, 0, 0, 5);
    SetTileAnimationCmd cmd{std::vector<SetTileAnimationCmd::Entry>{
        {0, 0, 0, /*oldAnim=*/-1, /*newAnim=*/animId, /*oldTileId=*/5}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetTileAnimation(0, 0, 0), animId);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetTileAnimation(0, 0, 0), -1);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), 5);
}

// --- SetTileStructureIdsCmd -------------------------------------------------

TEST_F(FlagToggleCmdTest, SetTileStructureIds_RoundTrip)
{
    int sid = tilemap.AddNoProjectionStructure({0, 0}, {64, 64});

    SetTileStructureIdsCmd cmd{std::vector<SetTileStructureIdsCmd::Entry>{
        {2, 2, 1, /*old=*/-1, /*new=*/sid}, {3, 3, 1, -1, sid}}};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetTileStructureId(2, 2, 1), sid);
    EXPECT_EQ(tilemap.GetTileStructureId(3, 3, 1), sid);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetTileStructureId(2, 2, 1), -1);
    EXPECT_EQ(tilemap.GetTileStructureId(3, 3, 1), -1);
}

// --- CompositeCmd -----------------------------------------------------------

TEST_F(FlagToggleCmdTest, CompositeCmd_AppliesAllInOrder)
{
    int sid = tilemap.AddNoProjectionStructure({0, 0}, {64, 64});

    std::vector<std::unique_ptr<EditorCommand>> children;
    children.push_back(std::make_unique<NoProjectionToggleCmd>(
        std::vector<LayerFlagEntry>{{1, 1, 0, false, true}}));
    children.push_back(std::make_unique<SetTileStructureIdsCmd>(
        std::vector<SetTileStructureIdsCmd::Entry>{{1, 1, 1, -1, sid}}));

    CompositeCmd cmd{"Structure assign", std::move(children)};
    cmd.Apply(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetLayerNoProjection(1, 1, 0));
    EXPECT_EQ(tilemap.GetTileStructureId(1, 1, 1), sid);

    cmd.Revert(tilemap, npcs);
    EXPECT_FALSE(tilemap.GetLayerNoProjection(1, 1, 0));
    EXPECT_EQ(tilemap.GetTileStructureId(1, 1, 1), -1);
}

// --- AddStructureCmd / RemoveStructureCmd -----------------------------------

class StructureCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(StructureCmdTest, AddStructure_AddsAndAssignsId)
{
    AddStructureCmd cmd{glm::vec2{0, 0}, glm::vec2{64, 64}, "test"};
    cmd.Apply(tilemap, npcs);
    EXPECT_GE(cmd.StructureId(), 0);
    const auto* s = tilemap.GetNoProjectionStructure(cmd.StructureId());
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->name, "test");
}

TEST_F(StructureCmdTest, AddStructure_RevertRemoves)
{
    AddStructureCmd cmd{glm::vec2{0, 0}, glm::vec2{64, 64}};
    cmd.Apply(tilemap, npcs);
    int id = cmd.StructureId();
    ASSERT_NE(tilemap.GetNoProjectionStructure(id), nullptr);
    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetNoProjectionStructure(id), nullptr);
}

TEST_F(StructureCmdTest, AddStructure_RoundTripPreservesId)
{
    UndoRedoStack stack;
    auto cmd = std::make_unique<AddStructureCmd>(glm::vec2{0, 0}, glm::vec2{64, 64}, "round");
    auto* cmdPtr = cmd.get();
    stack.Execute(std::move(cmd), tilemap, npcs);
    int id = cmdPtr->StructureId();

    stack.Undo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetNoProjectionStructure(id), nullptr);
    stack.Redo(tilemap, npcs);
    const auto* s = tilemap.GetNoProjectionStructure(id);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->name, "round");
}

TEST_F(StructureCmdTest, RemoveStructure_RestoresStructAndTileRefs)
{
    int sid = tilemap.AddNoProjectionStructure({0, 0}, {32, 32}, "kept");
    tilemap.SetTileStructureId(1, 1, 1, sid);
    tilemap.SetTileStructureId(2, 2, 1, sid);

    RemoveStructureCmd cmd{sid};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetNoProjectionStructure(sid), nullptr);
    EXPECT_EQ(tilemap.GetTileStructureId(1, 1, 1), -1);
    EXPECT_EQ(tilemap.GetTileStructureId(2, 2, 1), -1);

    cmd.Revert(tilemap, npcs);
    const auto* s = tilemap.GetNoProjectionStructure(sid);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->name, "kept");
    EXPECT_EQ(tilemap.GetTileStructureId(1, 1, 1), sid);
    EXPECT_EQ(tilemap.GetTileStructureId(2, 2, 1), sid);
}

TEST_F(StructureCmdTest, RemoveStructure_MidVector_PreservesHigherStructures)
{
    int a = tilemap.AddNoProjectionStructure({0, 0}, {16, 16}, "A");
    int b = tilemap.AddNoProjectionStructure({0, 0}, {16, 16}, "B");
    int c = tilemap.AddNoProjectionStructure({0, 0}, {16, 16}, "C");
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, 2);

    RemoveStructureCmd cmd{b};
    cmd.Apply(tilemap, npcs);
    // After remove, C shifted from id=2 to id=1.
    EXPECT_EQ(tilemap.GetNoProjectionStructure(0)->name, "A");
    EXPECT_EQ(tilemap.GetNoProjectionStructure(1)->name, "C");

    cmd.Revert(tilemap, npcs);
    // B reinstated at id=1, C shifts back to id=2.
    EXPECT_EQ(tilemap.GetNoProjectionStructure(0)->name, "A");
    EXPECT_EQ(tilemap.GetNoProjectionStructure(1)->name, "B");
    EXPECT_EQ(tilemap.GetNoProjectionStructure(2)->name, "C");
}

// --- AddParticleZoneCmd / RemoveParticleZoneCmd -----------------------------

class ParticleZoneCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }

    static ParticleZone MakeZone(float x, float y, ParticleType type = ParticleType::Firefly)
    {
        ParticleZone z;
        z.position = {x, y};
        z.size = {16, 16};
        z.type = type;
        z.enabled = true;
        z.noProjection = false;
        return z;
    }
};

TEST_F(ParticleZoneCmdTest, AddZone_RoundTrip)
{
    AddParticleZoneCmd cmd{MakeZone(10.0f, 20.0f)};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetParticleZones()->size(), 1u);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetParticleZones()->size(), 0u);

    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(tilemap.GetParticleZones()->size(), 1u);
    EXPECT_FLOAT_EQ((*tilemap.GetParticleZones())[0].position.x, 10.0f);
}

TEST_F(ParticleZoneCmdTest, RemoveZone_PreservesIndex)
{
    tilemap.AddParticleZone(MakeZone(0, 0));    // idx 0
    tilemap.AddParticleZone(MakeZone(10, 10));  // idx 1
    tilemap.AddParticleZone(MakeZone(20, 20));  // idx 2

    RemoveParticleZoneCmd cmd{1};
    cmd.Apply(tilemap, npcs);
    ASSERT_EQ(tilemap.GetParticleZones()->size(), 2u);
    // After removal, the zone that was at idx 2 shifts to idx 1.
    EXPECT_FLOAT_EQ((*tilemap.GetParticleZones())[1].position.x, 20.0f);

    cmd.Revert(tilemap, npcs);
    ASSERT_EQ(tilemap.GetParticleZones()->size(), 3u);
    EXPECT_FLOAT_EQ((*tilemap.GetParticleZones())[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ((*tilemap.GetParticleZones())[1].position.x, 10.0f);
    EXPECT_FLOAT_EQ((*tilemap.GetParticleZones())[2].position.x, 20.0f);
}

// --- PasteRegionCmd ---------------------------------------------------------

class PasteRegionCmdTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    std::vector<NonPlayerCharacter> npcs;

    void SetUp() override { tilemap.SetTilemapSize(8, 8, false); }
};

TEST_F(PasteRegionCmdTest, Snapshot_CapturesAllFields)
{
    tilemap.SetLayerTile(2, 3, 0, 42);
    tilemap.SetLayerRotation(2, 3, 0, 90.0f);
    tilemap.SetTileCollision(2, 3, true);
    tilemap.SetElevation(2, 3, 5);

    auto region = PasteRegionCmd::SnapshotRegion(tilemap, 2, 3, 1, 1);
    ASSERT_EQ(region.cells.size(), 1u);
    EXPECT_EQ(region.cells[0].layers[0].tileId, 42);
    EXPECT_FLOAT_EQ(region.cells[0].layers[0].rotation, 90.0f);
    EXPECT_TRUE(region.cells[0].collision);
    EXPECT_EQ(region.cells[0].elevation, 5);
}

TEST_F(PasteRegionCmdTest, Paste_WritesAndRevertRestores)
{
    // Source region: 2x2 with mixed data.
    tilemap.SetLayerTile(0, 0, 0, 11);
    tilemap.SetLayerTile(1, 0, 0, 12);
    tilemap.SetLayerTile(0, 1, 0, 21);
    tilemap.SetLayerTile(1, 1, 0, 22);
    auto region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 2, 2);

    // Pre-paste destination: tile at (5,5) is set to 99.
    tilemap.SetLayerTile(5, 5, 0, 99);
    tilemap.SetLayerTile(6, 5, 0, 88);
    tilemap.SetLayerTile(5, 6, 0, 77);
    tilemap.SetLayerTile(6, 6, 0, 66);

    PasteRegionCmd cmd{5, 5, region};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(5, 5, 0), 11);
    EXPECT_EQ(tilemap.GetLayerTile(6, 5, 0), 12);
    EXPECT_EQ(tilemap.GetLayerTile(5, 6, 0), 21);
    EXPECT_EQ(tilemap.GetLayerTile(6, 6, 0), 22);

    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(5, 5, 0), 99);
    EXPECT_EQ(tilemap.GetLayerTile(6, 5, 0), 88);
    EXPECT_EQ(tilemap.GetLayerTile(5, 6, 0), 77);
    EXPECT_EQ(tilemap.GetLayerTile(6, 6, 0), 66);
}

TEST_F(PasteRegionCmdTest, Paste_OutOfBounds_ClampsAndRevertWorks)
{
    tilemap.SetLayerTile(0, 0, 0, 5);
    auto region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 2, 2);

    // Paste at (7, 7) - half the region would land out-of-bounds (8x8 map).
    tilemap.SetLayerTile(7, 7, 0, 999);
    PasteRegionCmd cmd{7, 7, region};
    cmd.Apply(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(7, 7, 0), 5);  // clamped paste worked
    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(7, 7, 0), 999);
}

TEST_F(PasteRegionCmdTest, Paste_PreservesCollisionAndElevation)
{
    tilemap.SetTileCollision(0, 0, true);
    tilemap.SetElevation(0, 0, 16);
    auto region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 1, 1);

    tilemap.SetTileCollision(4, 4, false);
    tilemap.SetElevation(4, 4, 0);

    PasteRegionCmd cmd{4, 4, region};
    cmd.Apply(tilemap, npcs);
    EXPECT_TRUE(tilemap.GetTileCollision(4, 4));
    EXPECT_EQ(tilemap.GetElevation(4, 4), 16);

    cmd.Revert(tilemap, npcs);
    EXPECT_FALSE(tilemap.GetTileCollision(4, 4));
    EXPECT_EQ(tilemap.GetElevation(4, 4), 0);
}

TEST_F(PasteRegionCmdTest, EmptyClipboard_NoOp)
{
    ClipboardRegion empty;  // width=0, height=0
    PasteRegionCmd cmd{0, 0, empty};
    cmd.Apply(tilemap, npcs);
    cmd.Revert(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(0, 0, 0), -1);
}

TEST_F(PasteRegionCmdTest, StackIntegration_RoundTrip)
{
    tilemap.SetLayerTile(0, 0, 0, 7);
    auto region = PasteRegionCmd::SnapshotRegion(tilemap, 0, 0, 1, 1);

    UndoRedoStack stack;
    stack.Execute(std::make_unique<PasteRegionCmd>(3, 3, region), tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(3, 3, 0), 7);

    stack.Undo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(3, 3, 0), -1);

    stack.Redo(tilemap, npcs);
    EXPECT_EQ(tilemap.GetLayerTile(3, 3, 0), 7);
}
