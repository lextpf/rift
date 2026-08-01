#pragma once

#include "EnumTraits.hpp"

#include <cstddef>
#include <iterator>
#include <string_view>

/**
 * @brief Blend and depth state selectors for the world-space 3D draw path.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Kept in their own tiny header rather than inside @c IRenderer.hpp so that
 * geometry-building code (and its tests) can name a pass without pulling in the
 * whole renderer interface.
 *
 * @par The pass contract
 * A world frame draws in a fixed order, and each pass is defined by the pair of
 * modes it submits with:
 *
 * | Pass           | Depth           | Blend                  | Contents                       |
 * |----------------|-----------------|------------------------|--------------------------------|
 * | A0 ground      | @c None         | @c Alpha (cutout)      | flat ground tiles, layer order |
 * | A1 opaque      | @c TestAndWrite | @c Alpha (cutout)      | upright tiles, actors          |
 * | B translucent  | @c TestOnly     | @c Alpha / @c Additive | particles, light pools         |
 *
 * Pass A0 turns depth off because every ground layer of a cell is coplanar, and
 * rotated quads spill into neighbouring cells where coplanar surfaces of different
 * geometry z-fight. Authored layer order carries the ordering instead.
 *
 * Pass A1 relies on the fragment shader's alpha cutout: fragments below the
 * threshold are discarded, so they write neither color nor depth and cutout
 * artwork gets correct occlusion **without any sorting**. That is what lets the
 * opaque pass batch purely by texture.
 *
 * Pass B is **planned, not shipped**: particles, light pools and shadows still
 * draw on the flat 2.5D pipeline, and @c TestOnly has no production caller today.
 * When it lands it must be submitted back-to-front, because blended fragments are
 * order-dependent - but it will not write depth, so translucent geometry never
 * occludes anything.
 *
 * Screen-space UI is not a pass here at all: it goes through the 2D primitives,
 * which take no @ref DepthMode.
 */
namespace renderModes
{

/// @brief How a draw's color combines with what is already in the target.
enum class BlendMode
{
    /// Standard `src.a, 1 - src.a` compositing.
    Alpha = 0,
    /// `src.a, 1` accumulation for glows (fireflies, sparkles, light pools).
    Additive = 1
};

/// @brief How a draw interacts with the depth buffer.
enum class DepthMode
{
    /// Ignore depth entirely: no test, no write. The flat ground sheet, whose
    /// layers are coplanar and must keep authored layer order.
    None = 0,
    /// Test against existing depth but do not write. Reserved for translucent world
    /// geometry, which must not occlude whatever is drawn after it; no caller yet.
    TestOnly = 1,
    /// Test and write. Opaque (alpha-cutout) world geometry.
    TestAndWrite = 2
};

/// @brief Number of entries in @ref BlendMode.
inline constexpr std::size_t BLEND_MODE_COUNT = 2;
/// @brief Number of entries in @ref DepthMode.
inline constexpr std::size_t DEPTH_MODE_COUNT = 3;

/**
 * @brief Alpha below which a fragment is discarded in the opaque pass.
 *
 * Pixel-art sprites have hard edges, so a mid-range cutoff cleanly separates
 * "solid" from "empty" without eating antialiased fringes. Comparable tile-map
 * tools cut at 0.9; Rift's artwork has less partial alpha around silhouettes, and
 * 0.5 keeps single-pixel details that 0.9 would erode.
 */
inline constexpr float OPAQUE_ALPHA_CUTOFF = 0.5f;

}  // namespace renderModes

/// @brief Reflection for @ref renderModes::BlendMode (debug output, console).
template <>
struct EnumTraits<renderModes::BlendMode>
    : EnumTraitsBase<renderModes::BlendMode, EnumTraits<renderModes::BlendMode>>
{
    static constexpr std::size_t Count = renderModes::BLEND_MODE_COUNT;
    static constexpr std::string_view Names[] = {"Alpha", "Additive"};
};

/// @brief Reflection for @ref renderModes::DepthMode (debug output, console).
template <>
struct EnumTraits<renderModes::DepthMode>
    : EnumTraitsBase<renderModes::DepthMode, EnumTraits<renderModes::DepthMode>>
{
    static constexpr std::size_t Count = renderModes::DEPTH_MODE_COUNT;
    static constexpr std::string_view Names[] = {"None", "TestOnly", "TestAndWrite"};
};

static_assert(std::size(EnumTraits<renderModes::BlendMode>::Names) == renderModes::BLEND_MODE_COUNT,
              "renderModes::BlendMode names must stay in step with BLEND_MODE_COUNT");
static_assert(std::size(EnumTraits<renderModes::DepthMode>::Names) == renderModes::DEPTH_MODE_COUNT,
              "renderModes::DepthMode names must stay in step with DEPTH_MODE_COUNT");
