#pragma once

#include "Texture.hpp"
#include "TextureHandle.hpp"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

class IRenderer;

/**
 * @class TextureStore
 * @brief Renderer-side owner of sprite textures, addressed by @ref TextureHandle.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Centralizes ownership of the textures that were previously owned by the
 * character classes. An entity (and, later, an ECS @c Sprite component) holds a
 * @ref TextureHandle instead of an owned @ref Texture; the store keeps the one
 * copy and is the single place the renderer-switch re-upload iterates. This is
 * the seam the future ECS @c on_add<Sprite> upload hook drives.
 *
 * @par Deduplication
 * @ref Acquire dedups by path, so many NPCs of the same type share one texture.
 * @ref Adopt takes ownership of an already-built (e.g. procedural / in-memory)
 * texture and is never deduped.
 *
 * @par Pointer stability
 * Textures live in a node-based @c std::unordered_map, so a @c const @c Texture&
 * obtained from @ref Get stays valid across later @ref Acquire / @ref Adopt
 * calls (only erasure would invalidate it, and the store never erases).
 */
class TextureStore
{
public:
    /// @brief Load-or-get a texture by path (deduped). Tries @p path, then
    /// @c "../" + @p path. Returns an invalid handle if the file cannot load.
    TextureHandle Acquire(const std::string& path);

    /// @brief Take ownership of an already-built texture (not deduped).
    TextureHandle Adopt(Texture&& texture);

    /// @brief True if @p handle refers to a loaded texture.
    [[nodiscard]] bool IsValid(TextureHandle handle) const;

    /// @brief Resolve @p handle. Invalid handles resolve to a shared empty
    /// texture so callers can render/sample without null checks.
    [[nodiscard]] const Texture& Get(TextureHandle handle) const;

    /// @brief (Re)upload every owned texture into @p renderer (renderer switch).
    void UploadAll(IRenderer& renderer) const;

    /// @brief Accent color for a handle's texture (invalid/empty -> @p fallback).
    [[nodiscard]] glm::vec3 SampleAccent(TextureHandle handle, glm::vec3 fallback) const;

    /// @brief Number of textures owned.
    [[nodiscard]] std::size_t Count() const { return m_Textures.size(); }

private:
    std::unordered_map<std::string, TextureHandle> m_ByPath;  ///< Dedup index for Acquire.
    std::unordered_map<AssetId, Texture> m_Textures;          ///< The owned textures.
    AssetId m_NextId = 1;                                     ///< Next id to mint (0 = invalid).
};
