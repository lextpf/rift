#pragma once

#include <vector>

/**
 * @brief Pure, renderer-free procedural texture generation for AuroraNight.
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
 * Gaussian horizontal feather + asymmetric vertical profile (soft top, fuller
 * base) + faint vertical ray striations. Inherently translucent.
 *
 * @param width   Texture width in pixels.
 * @param height  Texture height in pixels.
 * @return        RGBA8 pixels, tightly packed (@p width * @p height * 4 bytes).
 */
std::vector<unsigned char> BuildCurtainPixels(int width, int height);

/**
 * @brief Build a vertical oval beam/ray texture.
 *
 * Gaussian oval cross-section, brightest in the lower third, feathering to
 * nothing at the top with a soft base edge.
 *
 * @param width   Texture width in pixels.
 * @param height  Texture height in pixels.
 * @return        RGBA8 pixels, tightly packed (@p width * @p height * 4 bytes).
 */
std::vector<unsigned char> BuildBeamPixels(int width, int height);
}  // namespace AuroraTextures
