#include "TextureStore.hpp"

#include "IRenderer.hpp"

namespace
{
/// Shared empty texture returned for invalid handles so callers never deref null.
const Texture& EmptyTexture()
{
    static const Texture empty;
    return empty;
}
}  // namespace

TextureHandle TextureStore::Acquire(const std::string& path)
{
    if (auto it = m_ByPath.find(path); it != m_ByPath.end())
    {
        return it->second;
    }

    Texture tex;
    // Match the historical per-class loader behavior: try the given path, then a
    // parent-directory fallback (handy when the working directory is build/).
    if (!tex.LoadFromFile(path) && !tex.LoadFromFile("../" + path))
    {
        return TextureHandle{};  // invalid
    }

    const AssetId id = m_NextId++;
    m_Textures.emplace(id, std::move(tex));
    const TextureHandle handle{id};
    m_ByPath.emplace(path, handle);
    return handle;
}

TextureHandle TextureStore::Adopt(Texture&& texture)
{
    const AssetId id = m_NextId++;
    m_Textures.emplace(id, std::move(texture));
    return TextureHandle{id};
}

bool TextureStore::IsValid(TextureHandle handle) const
{
    return handle.id != 0 && m_Textures.find(handle.id) != m_Textures.end();
}

const Texture& TextureStore::Get(TextureHandle handle) const
{
    if (auto it = m_Textures.find(handle.id); it != m_Textures.end())
    {
        return it->second;
    }
    return EmptyTexture();
}

void TextureStore::UploadAll(IRenderer& renderer) const
{
    for (const auto& [id, tex] : m_Textures)
    {
        renderer.UploadTexture(tex);
    }
}

glm::vec3 TextureStore::SampleAccent(TextureHandle handle, glm::vec3 fallback) const
{
    return Get(handle).SampleDominantNonSkinColor(fallback);
}
