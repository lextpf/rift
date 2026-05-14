// Tests for the brush rotation+flip source mapping used by the editor's
// placement preview and multi-tile paint. The math lives in a header so it
// can be exercised without linking the editor's input pipeline (which pulls
// in dialogue tree builders excluded from the test build).

#include <gtest/gtest.h>

#include "../src/EditorBrushTransform.hpp"

namespace
{

BrushSourceCoord Source(int dx, int dy, int width, int height, int rotation, bool flipX, bool flipY)
{
    return CalculateBrushSourceTile(dx, dy, BrushTransform{width, height, rotation, flipX, flipY});
}

}  // namespace

TEST(EditorBrushTransform, IdentityNoFlipNoRotation)
{
    auto r0 = Source(0, 0, 3, 2, 0, false, false);
    EXPECT_EQ(r0.sourceDx, 0);
    EXPECT_EQ(r0.sourceDy, 0);

    auto r1 = Source(2, 1, 3, 2, 0, false, false);
    EXPECT_EQ(r1.sourceDx, 2);
    EXPECT_EQ(r1.sourceDy, 1);
}

TEST(EditorBrushTransform, FlipXMirrorsColumnsAtRotationZero)
{
    auto left = Source(0, 0, 3, 2, 0, true, false);
    EXPECT_EQ(left.sourceDx, 2);
    EXPECT_EQ(left.sourceDy, 0);

    auto right = Source(2, 0, 3, 2, 0, true, false);
    EXPECT_EQ(right.sourceDx, 0);
    EXPECT_EQ(right.sourceDy, 0);

    auto middle = Source(1, 1, 3, 2, 0, true, false);
    EXPECT_EQ(middle.sourceDx, 1);
    EXPECT_EQ(middle.sourceDy, 1);
}

TEST(EditorBrushTransform, FlipYMirrorsRowsAtRotationZero)
{
    auto top = Source(0, 0, 2, 3, 0, false, true);
    EXPECT_EQ(top.sourceDx, 0);
    EXPECT_EQ(top.sourceDy, 2);

    auto bottom = Source(0, 2, 2, 3, 0, false, true);
    EXPECT_EQ(bottom.sourceDx, 0);
    EXPECT_EQ(bottom.sourceDy, 0);
}

TEST(EditorBrushTransform, FlipXAndFlipYMirrorsBoth)
{
    auto r = Source(0, 0, 3, 2, 0, true, true);
    EXPECT_EQ(r.sourceDx, 2);
    EXPECT_EQ(r.sourceDy, 1);
}

TEST(EditorBrushTransform, Rotation90MatchesExistingBehavior)
{
    // 2x1 brush. Existing CalculateRotatedSourceTile at rotation=90:
    //   sourceDx = width - 1 - dy, sourceDy = dx
    auto a = Source(0, 0, 2, 1, 90, false, false);
    EXPECT_EQ(a.sourceDx, 1);
    EXPECT_EQ(a.sourceDy, 0);

    auto b = Source(0, 1, 2, 1, 90, false, false);
    EXPECT_EQ(b.sourceDx, 0);
    EXPECT_EQ(b.sourceDy, 0);
}

TEST(EditorBrushTransform, Rotation180MatchesExistingBehavior)
{
    auto r = Source(1, 0, 3, 2, 180, false, false);
    EXPECT_EQ(r.sourceDx, 1);
    EXPECT_EQ(r.sourceDy, 1);
}

TEST(EditorBrushTransform, Rotation270MatchesExistingBehavior)
{
    // rotation=270: sourceDx = dy, sourceDy = height - 1 - dx
    auto r = Source(0, 0, 2, 3, 270, false, false);
    EXPECT_EQ(r.sourceDx, 0);
    EXPECT_EQ(r.sourceDy, 2);
}

TEST(EditorBrushTransform, FlipXThenRotation90_2x2)
{
    // 2x2 brush:
    //   |A|B|
    //   |C|D|
    // Rotation 90 alone (no flip):
    //   |B|D|
    //   |A|C|
    // Plus flipX (mirror columns, set per-cell flipX flag):
    //   |D|B|
    //   |C|A|
    auto topLeft = Source(0, 0, 2, 2, 90, true, false);  // D at source (1,1)
    EXPECT_EQ(topLeft.sourceDx, 1);
    EXPECT_EQ(topLeft.sourceDy, 1);

    auto topRight = Source(1, 0, 2, 2, 90, true, false);  // B at source (1,0)
    EXPECT_EQ(topRight.sourceDx, 1);
    EXPECT_EQ(topRight.sourceDy, 0);

    auto botLeft = Source(0, 1, 2, 2, 90, true, false);  // C at source (0,1)
    EXPECT_EQ(botLeft.sourceDx, 0);
    EXPECT_EQ(botLeft.sourceDy, 1);

    auto botRight = Source(1, 1, 2, 2, 90, true, false);  // A at source (0,0)
    EXPECT_EQ(botRight.sourceDx, 0);
    EXPECT_EQ(botRight.sourceDy, 0);
}

TEST(EditorBrushTransform, FlipXIsInvolutionAcrossDestination)
{
    // Source(dx, dy, ..., flipX=false) must equal Source(destW-1-dx, dy, ..., flipX=true).
    for (int rot : {0, 90, 180, 270})
    {
        const int destW = (rot == 90 || rot == 270) ? 2 : 3;
        const int destH = (rot == 90 || rot == 270) ? 3 : 2;
        for (int dy = 0; dy < destH; ++dy)
        {
            for (int dx = 0; dx < destW; ++dx)
            {
                auto plain = Source(dx, dy, 3, 2, rot, false, false);
                auto mirrored = Source(destW - 1 - dx, dy, 3, 2, rot, true, false);
                EXPECT_EQ(mirrored.sourceDx, plain.sourceDx)
                    << "rot=" << rot << " dx=" << dx << " dy=" << dy;
                EXPECT_EQ(mirrored.sourceDy, plain.sourceDy)
                    << "rot=" << rot << " dx=" << dx << " dy=" << dy;
            }
        }
    }
}
