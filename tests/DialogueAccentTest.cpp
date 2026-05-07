// Tests for the per-NPC accent color sampler used by the dialogue panel.
// Exercises Texture::SampleDominantNonSkinColor (pure pixel-data math) and the
// lazy caching contract on NonPlayerCharacter::GetAccentColor. No GL/Vulkan
// context is created.

#include <gtest/gtest.h>

#include "../src/NonPlayerCharacter.h"
#include "../src/Texture.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
constexpr int kSize = 4;      // 4x4 = 16 pixels - plenty for filter tests
constexpr int kChannels = 4;  // RGBA

using Pixel = std::array<unsigned char, 4>;

/// Build a Texture from a flat 4x4 RGBA buffer. Loads via LoadFromData with no
/// flipY (we want byte-for-byte fidelity in the test pixel layout).
Texture MakeTexture(const std::vector<unsigned char>& pixels)
{
    Texture tex;
    auto buffer = pixels;  // LoadFromData takes non-const pointer
    EXPECT_TRUE(tex.LoadFromData(buffer.data(), kSize, kSize, kChannels, /*flipY=*/false));
    return tex;
}

/// Fill a 4x4 RGBA buffer uniformly with the given pixel value.
std::vector<unsigned char> Uniform(Pixel p)
{
    std::vector<unsigned char> out(kSize * kSize * kChannels);
    for (int i = 0; i < kSize * kSize; ++i)
    {
        out[i * 4 + 0] = p[0];
        out[i * 4 + 1] = p[1];
        out[i * 4 + 2] = p[2];
        out[i * 4 + 3] = p[3];
    }
    return out;
}

/// Fill a 4x4 buffer with the given background and overwrite a single pixel.
std::vector<unsigned char> WithOnePixel(Pixel bg, int x, int y, Pixel one)
{
    auto out = Uniform(bg);
    const int idx = (y * kSize + x) * kChannels;
    out[idx + 0] = one[0];
    out[idx + 1] = one[1];
    out[idx + 2] = one[2];
    out[idx + 3] = one[3];
    return out;
}

constexpr glm::vec3 kFallback{0.85f, 0.75f, 0.40f};

bool ColorsClose(glm::vec3 a, glm::vec3 b, float eps = 1e-3f)
{
    return std::fabs(a.r - b.r) < eps && std::fabs(a.g - b.g) < eps && std::fabs(a.b - b.b) < eps;
}
}  // namespace

TEST(DialogueAccentTest, AllTransparent_ReturnsFallback)
{
    auto tex = MakeTexture(Uniform({255, 100, 100, 0}));  // saturated but alpha=0
    EXPECT_TRUE(ColorsClose(tex.SampleDominantNonSkinColor(kFallback), kFallback));
}

TEST(DialogueAccentTest, AllSkinTone_ReturnsFallback)
{
    // #d2a07c - mid skin tone. Hue ~25deg, sat ~0.40, value ~0.82.
    // Falls inside the skin-band [0deg, 30deg] AND sat [0.20, 0.60] -> filtered.
    auto tex = MakeTexture(Uniform({0xd2, 0xa0, 0x7c, 255}));
    EXPECT_TRUE(ColorsClose(tex.SampleDominantNonSkinColor(kFallback), kFallback));
}

TEST(DialogueAccentTest, AllGrey_ReturnsFallback)
{
    // sat = 0 -> below the 0.30 saturation threshold.
    auto tex = MakeTexture(Uniform({128, 128, 128, 255}));
    EXPECT_TRUE(ColorsClose(tex.SampleDominantNonSkinColor(kFallback), kFallback));
}

TEST(DialogueAccentTest, AllDarkShadow_ReturnsFallback)
{
    // value ~0.23 -> below the 0.25 threshold.
    auto tex = MakeTexture(Uniform({0x1a, 0x1a, 0x3a, 255}));
    EXPECT_TRUE(ColorsClose(tex.SampleDominantNonSkinColor(kFallback), kFallback));
}

TEST(DialogueAccentTest, AllNearWhiteHighlight_ReturnsFallback)
{
    // value > 0.95 -> filtered as highlight.
    auto tex = MakeTexture(Uniform({250, 250, 250, 255}));
    EXPECT_TRUE(ColorsClose(tex.SampleDominantNonSkinColor(kFallback), kFallback));
}

TEST(DialogueAccentTest, BrightRedAmongGrey_PicksRed)
{
    // Single saturated red pixel among 15 grey pixels -> red wins.
    auto tex = MakeTexture(WithOnePixel(/*bg=*/{128, 128, 128, 255},
                                        /*x=*/2,
                                        /*y=*/1,
                                        /*one=*/{220, 30, 30, 255}));
    const glm::vec3 result = tex.SampleDominantNonSkinColor(kFallback);
    EXPECT_NEAR(result.r, 220.0f / 255.0f, 1e-3f);
    EXPECT_NEAR(result.g, 30.0f / 255.0f, 1e-3f);
    EXPECT_NEAR(result.b, 30.0f / 255.0f, 1e-3f);
}

TEST(DialogueAccentTest, MixedSaturated_PicksMostVibrant)
{
    // Two candidates: bright cyan (sat ~0.96, value ~0.85) and dim purple
    // (sat ~0.50, value ~0.40). Cyan has higher (s * v) and should win.
    std::vector<unsigned char> pixels(kSize * kSize * kChannels, 0);
    // Fill background with greys (filtered out)
    for (int i = 0; i < kSize * kSize; ++i)
    {
        pixels[i * 4 + 0] = 100;
        pixels[i * 4 + 1] = 100;
        pixels[i * 4 + 2] = 100;
        pixels[i * 4 + 3] = 255;
    }
    // Cyan at (0,0)
    pixels[0] = 10;
    pixels[1] = 220;
    pixels[2] = 220;
    pixels[3] = 255;
    // Dim purple at (3,3)
    const int dimIdx = (3 * kSize + 3) * 4;
    pixels[dimIdx + 0] = 80;
    pixels[dimIdx + 1] = 40;
    pixels[dimIdx + 2] = 100;
    pixels[dimIdx + 3] = 255;

    auto tex = MakeTexture(pixels);
    const glm::vec3 result = tex.SampleDominantNonSkinColor(kFallback);
    EXPECT_NEAR(result.r, 10.0f / 255.0f, 1e-3f);
    EXPECT_NEAR(result.g, 220.0f / 255.0f, 1e-3f);
    EXPECT_NEAR(result.b, 220.0f / 255.0f, 1e-3f);
}

TEST(DialogueAccentTest, MidSaturationOrange_NotMisclassifiedAsSkin)
{
    // #ff8030 - bright orange. Hue ~17deg (inside skin hue band), but saturation
    // is ~0.81 which is above the kSkinSatMax = 0.60 cap. So it escapes the
    // skin filter and is accepted as a valid accent.
    auto tex = MakeTexture(Uniform({0xff, 0x80, 0x30, 255}));
    const glm::vec3 result = tex.SampleDominantNonSkinColor(kFallback);
    EXPECT_NEAR(result.r, 1.0f, 1e-3f);
    EXPECT_NEAR(result.g, 0x80 / 255.0f, 1e-3f);
    EXPECT_NEAR(result.b, 0x30 / 255.0f, 1e-3f);
}

TEST(DialogueAccentTest, EmptyTexture_ReturnsFallback)
{
    Texture tex;  // never loaded
    EXPECT_TRUE(ColorsClose(tex.SampleDominantNonSkinColor(kFallback), kFallback));
}

TEST(DialogueAccentTest, NpcAccent_CachesAcrossCalls)
{
    // Build a synthetic sprite with one bright red pixel so the accent has a
    // deterministic, non-fallback value.
    auto pixels = WithOnePixel(/*bg=*/{128, 128, 128, 255},
                               /*x=*/0,
                               /*y=*/0,
                               /*one=*/{220, 30, 30, 255});
    Texture tex = MakeTexture(pixels);

    NonPlayerCharacter npc;
    npc.GetSpriteSheet() = std::move(tex);

    // First call computes; second call returns the cached value. We can't easily
    // observe the cache directly (it's private), but we can prove the function is
    // deterministic - same input -> same output across repeated calls.
    const glm::vec3 first = npc.GetAccentColor();
    const glm::vec3 second = npc.GetAccentColor();
    EXPECT_TRUE(ColorsClose(first, second));
    // And the result is the bright red, not the fallback.
    EXPECT_NEAR(first.r, 220.0f / 255.0f, 1e-3f);
    EXPECT_NEAR(first.g, 30.0f / 255.0f, 1e-3f);
    EXPECT_NEAR(first.b, 30.0f / 255.0f, 1e-3f);
}
