// Tests for TextureStore, the renderer-side texture owner that backs the future
// ECS Sprite component's TextureHandle. Pure data paths - textures are built via
// LoadFromData (no GL/Vulkan context), honoring the rift_tests constraint.

#include <gtest/gtest.h>

#include "../src/Texture.hpp"
#include "../src/TextureHandle.hpp"
#include "../src/TextureStore.hpp"

#include <vector>

namespace
{
// Build a 4x4 solid RGBA texture in CPU memory (no GL context required).
Texture MakeSolid(unsigned char r, unsigned char g, unsigned char b)
{
    std::vector<unsigned char> px(4 * 4 * 4);
    for (int i = 0; i < 16; ++i)
    {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = 255;
    }
    Texture t;
    t.LoadFromData(px.data(), 4, 4, 4, /*flipY=*/false);
    return t;
}
}  // namespace

TEST(TextureStore, DefaultHandleIsInvalid)
{
    TextureStore store;
    EXPECT_FALSE(store.IsValid(TextureHandle{}));
    EXPECT_EQ(store.Count(), static_cast<std::size_t>(0));
}

TEST(TextureStore, AdoptReturnsValidHandleAndGetResolves)
{
    TextureStore store;
    const TextureHandle h = store.Adopt(MakeSolid(10, 20, 30));
    EXPECT_TRUE(store.IsValid(h));
    EXPECT_EQ(store.Get(h).GetWidth(), 4);
    EXPECT_EQ(store.Get(h).GetHeight(), 4);
    EXPECT_EQ(store.Count(), static_cast<std::size_t>(1));
}

TEST(TextureStore, GetInvalidReturnsSharedEmptyTexture)
{
    TextureStore store;
    // Empty (never-loaded) texture has zero dimensions; deref is safe.
    EXPECT_EQ(store.Get(TextureHandle{}).GetWidth(), 0);
}

TEST(TextureStore, AdoptGivesDistinctHandles)
{
    TextureStore store;
    const TextureHandle a = store.Adopt(MakeSolid(1, 1, 1));
    const TextureHandle b = store.Adopt(MakeSolid(2, 2, 2));
    EXPECT_NE(a, b);
    EXPECT_EQ(store.Count(), static_cast<std::size_t>(2));
}

TEST(TextureStore, AcquireMissingFileReturnsInvalid)
{
    TextureStore store;
    const TextureHandle h = store.Acquire("does/not/exist_zzz_rift.png");
    EXPECT_FALSE(store.IsValid(h));
    EXPECT_EQ(store.Count(), static_cast<std::size_t>(0));
}

TEST(TextureStore, GetReferenceStableAcrossLaterAdopts)
{
    TextureStore store;
    const TextureHandle a = store.Adopt(MakeSolid(7, 8, 9));
    const Texture& first = store.Get(a);
    const void* addr = &first;
    // Force rehash/growth with several more adopts.
    for (int i = 0; i < 32; ++i)
    {
        store.Adopt(MakeSolid(static_cast<unsigned char>(i), 0, 0));
    }
    EXPECT_EQ(static_cast<const void*>(&store.Get(a)), addr);  // node-based: stable
}

TEST(TextureStore, SampleAccentInvalidReturnsFallback)
{
    TextureStore store;
    const glm::vec3 fallback{0.1f, 0.2f, 0.3f};
    const glm::vec3 c = store.SampleAccent(TextureHandle{}, fallback);
    EXPECT_FLOAT_EQ(c.r, fallback.r);
    EXPECT_FLOAT_EQ(c.g, fallback.g);
    EXPECT_FLOAT_EQ(c.b, fallback.b);
}
