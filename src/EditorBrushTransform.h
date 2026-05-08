#pragma once

/**
 * @brief Brush stamp transform (rotation + flip) for editor placement preview / paint.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Maps a destination cell (dx, dy) inside a rotated+flipped brush stamp to its
 * source cell (sourceDx, sourceDy) in the original brush layout. Destination
 * coordinates iterate over the post-rotation dimensions: at rotation 0/180 the
 * brush dimensions are unchanged; at 90/270 width and height are swapped.
 *
 * Flip semantics: flipX/flipY are applied in destination space first, then the
 * rotation is reversed to find the source. The renderer applies tileFlipX/Y
 * before rotation, so passing the same flag through to per-cell rendering
 * yields the user-expected "rotate the stamp, then flip" visual.
 */

struct BrushTransform
{
    int width;     ///< Brush source width in tiles (pre-rotation).
    int height;    ///< Brush source height in tiles (pre-rotation).
    int rotation;  ///< 0, 90, 180, or 270 degrees (CCW, matching the editor R-key).
    bool flipX;    ///< Mirror brush around the vertical axis.
    bool flipY;    ///< Mirror brush around the horizontal axis.
};

struct BrushSourceCoord
{
    int sourceDx;  ///< Source column index (0..width-1).
    int sourceDy;  ///< Source row index (0..height-1).
};

/// @brief Compute source brush cell for a destination cell after rotation+flip.
/// @param dx Destination column (0..rotated_dest_width-1).
/// @param dy Destination row (0..rotated_dest_height-1).
/// @param bt Brush transform parameters.
inline BrushSourceCoord CalculateBrushSourceTile(int dx, int dy, const BrushTransform& bt)
{
    const int destW = (bt.rotation == 90 || bt.rotation == 270) ? bt.height : bt.width;
    const int destH = (bt.rotation == 90 || bt.rotation == 270) ? bt.width : bt.height;

    const int dxe = bt.flipX ? (destW - 1 - dx) : dx;
    const int dye = bt.flipY ? (destH - 1 - dy) : dy;

    BrushSourceCoord r{};
    if (bt.rotation == 90)
    {
        r.sourceDx = bt.width - 1 - dye;
        r.sourceDy = dxe;
    }
    else if (bt.rotation == 180)
    {
        r.sourceDx = bt.width - 1 - dxe;
        r.sourceDy = bt.height - 1 - dye;
    }
    else if (bt.rotation == 270)
    {
        r.sourceDx = dye;
        r.sourceDy = bt.height - 1 - dxe;
    }
    else
    {
        r.sourceDx = dxe;
        r.sourceDy = dye;
    }
    return r;
}
