#pragma once

/**
 * @struct BrushTransform
 * @brief Brush stamp transform (rotation + flip) for editor placement preview / paint.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Describes how a rectangular multi-tile brush is oriented when stamped.
 * @ref CalculateBrushSourceTile consumes it to map a destination cell (dx, dy) inside the
 * transformed stamp back to its source cell (sourceDx, sourceDy) in the original brush
 * layout. Destination coordinates iterate over the post-rotation dimensions: at rotation
 * 0/180 the brush dimensions are unchanged; at 90/270 width and height are swapped.
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

/**
 * @struct BrushSourceCoord
 * @brief Offset of one cell inside the unrotated brush region.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Both members are offsets relative to the brush's top-left cell, not tileset or map
 * coordinates. Call sites turn them into a tileset id with
 * `selectedStartID + sourceDy * tilesPerRow + sourceDx`.
 */
struct BrushSourceCoord
{
    int sourceDx;  ///< Source column index (0..width-1).
    int sourceDy;  ///< Source row index (0..height-1).
};

/**
 * @brief Compute source brush cell for a destination cell after rotation+flip.
 *
 * The rotation is counter-clockwise, and the inverse mapping is applied in one step:
 * flips are undone in destination space, then the rotation is reversed.
 *
 * @par Worked example
 * A 3x2 brush (`width` = 3, `height` = 2) laid out as `A B C / D E F`, showing the
 * destination grid each setting produces. `dest(dx, dy)` reads the letter drawn there.
 * @verbatim
 *   source (rot 0, no flip)      rot 90 CCW           rot 180          rot 270 CCW
 *   dest is 3 wide, 2 tall       dest 2 x 3           dest 3 x 2       dest 2 x 3
 *
 *        A B C                     C F                 F E D              D A
 *        D E F                     B E                 C B A              E B
 *                                  A D                                    F C
 *
 *   flipX mirrors the DESTINATION columns, whatever the rotation:
 *
 *   rot 0 + flipX                rot 90 + flipX
 *        C B A                     F C
 *        F E D                     E B
 *                                  D A
 * @endverbatim
 * Reading one entry: at rotation 90 with no flip, `dest(0, 2)` shows `A`, and `A` sits at
 * source offset (0, 0), so the call returns `{sourceDx = 0, sourceDy = 0}`. Note the
 * dimension swap at 90/270 - `dy` there ranges over `width`, not `height`.
 *
 * @param dx Destination column (0..rotated_dest_width-1).
 * @param dy Destination row (0..rotated_dest_height-1).
 * @param bt Brush transform parameters.
 * @return Offset of the source cell within the unrotated brush region.
 */
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
