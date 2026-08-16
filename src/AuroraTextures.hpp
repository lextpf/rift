#pragma once

#include <vector>

/**
 * @brief Pure, renderer-free procedural texture generation for Aurora weather.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Each builder returns a tightly-packed RGBA8 buffer (width*height*4 bytes),
 * white RGB with a feathered alpha envelope, ready to upload as a texture with
 * no GPU dependency. Unit-tested in tests/AuroraTextureTests.cpp.
 */
namespace AuroraTextures
{
/**
 * @brief Build a wave/curtain sheet texture.
 *
 * Dual-envelope horizontal and vertical feathers (bright core, faint broad
 * halo) plus subtle ray striations. Inherently translucent.
 *
 * @param width   Texture width in pixels.
 * @param height  Texture height in pixels.
 * @return        RGBA8 pixels, tightly packed (@p width * @p height * 4 bytes).
 */
std::vector<unsigned char> BuildCurtainPixels(int width, int height);

/**
 * @brief Build a vertical oval beam/ray texture.
 *
 * Gaussian oval cross-section centered on the texture, so the beam is brightest
 * at mid-height and feathers to zero at both ends as well as both sides. That
 * makes it read as a floating, feathered glow rather than a spike rooted to
 * the band it is drawn over.
 *
 * @param width   Texture width in pixels.
 * @param height  Texture height in pixels.
 * @return        RGBA8 pixels, tightly packed (@p width * @p height * 4 bytes).
 */
std::vector<unsigned char> BuildBeamPixels(int width, int height);
}  // namespace AuroraTextures
