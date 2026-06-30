#pragma once

#include <vector>

/// Pure, renderer-free procedural texture generation for the AuroraNight effect.
/// Each builder returns tightly-packed RGBA8 (width*height*4 bytes), white RGB
/// with a feathered alpha envelope. Unit-tested in tests/AuroraTextureTests.cpp.
namespace AuroraTextures
{
/// Wave/curtain sheet: Gaussian horizontal feather + asymmetric vertical profile
/// (soft top, fuller base) + faint vertical ray striations. Inherently translucent.
std::vector<unsigned char> BuildCurtainPixels(int width, int height);

/// Vertical oval beam/ray: Gaussian oval cross-section, brightest in the lower
/// third, feathering to nothing at the top with a soft base edge.
std::vector<unsigned char> BuildBeamPixels(int width, int height);
}  // namespace AuroraTextures
