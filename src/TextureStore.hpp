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
 * Centralizes texture ownership: the sprite components (@ref PlayerSprite,
 * @ref NpcSprite) hold @ref TextureHandle values rather than owned @ref Texture
 * objects, so the store keeps the one copy and is the single place the
 * renderer-switch re-upload iterates.
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
 *
 * @par Renderer switch order
 * @ref UploadAll is one step of a fixed sequence in `Game::SwitchRenderer`. Two steps
 * of it are easy to get wrong: the GL context generation advances only when the new
 * backend is OpenGL, and the character atlas re-pack runs after @ref UploadAll and
 * deliberately overwrites the tileset upload.
 *
 * @htmlonly
 * <pre class="mermaid">
 * sequenceDiagram
 *     participant G as Game
 *     participant T as Texture (static)
 *     participant S as TextureStore
 *     participant R as IRenderer
 *     G->>R: Shutdown(), then destroy the window
 *     G->>R: create window, then create the new backend
 *     G->>T: AdvanceOpenGLContextGeneration()
 *     Note over G,T: OpenGL target only - skipped when switching to Vulkan
 *     G->>R: UploadTexture(tileset)
 *     G->>S: UploadAll(renderer)
 *     S->>R: UploadTexture(tex), once per owned texture
 *     Note over S,R: GL recreates only on generation mismatch; Vulkan always recreates
 *     G->>G: PackCharactersIntoAtlas()
 *     Note over G: repacks from CPU pixels and overwrites the tileset upload
 * </pre>
 * @endhtmlonly
 *
 * The store and its @ref Texture objects survive the switch, so handles and references
 * stay valid; only their GPU-side handles are rebuilt. Any cached @c IRenderer* does
 * dangle.
 *
 * @warning There is deliberately no release/erase API, and @c m_NextId only ever
 * increments, so handles are never recycled and no texture is ever freed before
 * the store itself is destroyed. Every texture the process touches is resident
 * for the lifetime of the owning @c Game. That is fine for @ref Acquire, which
 * dedups by path, but each @ref Adopt is a permanent allocation: adopting
 * procedural textures per world load grows the store without bound. Adopt once
 * at startup, or reuse the existing handle.
 */
class TextureStore
{
public:
    /**
     * @brief Load-or-get a texture by path, deduped by path.
     *
     * Tries @p path, then @c "../" + @p path. Only a successful load enters the dedup
     * index, so a path that fails is retried in full - two file-open attempts and two
     * Logger errors - on every call. Callers on a per-spawn path must not rely on the
     * index to suppress repeated failures.
     *
     * @param path  Texture file path.
     * @return      Handle to the loaded texture, or an invalid handle when it cannot load.
     */
    TextureHandle Acquire(const std::string& path);

    /// @brief Take ownership of an already-built texture (not deduped).
    TextureHandle Adopt(Texture&& texture);

    /// @brief True if @p handle refers to a loaded texture.
    [[nodiscard]] bool IsValid(TextureHandle handle) const;

    /// @brief Resolve @p handle. Invalid handles resolve to a shared empty
    /// texture so callers can render/sample without null checks.
    [[nodiscard]] const Texture& Get(TextureHandle handle) const;

    /**
     * @brief (Re)upload every owned texture into @p renderer (renderer switch).
     *
     * Synchronous, not a batched submit: each Vulkan upload blocks on its own fence, so
     * the cost is one GPU round-trip per texture. The Vulkan path can also throw
     * `std::runtime_error` out of @ref Texture::CreateVulkanTexture. The throw propagates
     * mid-iteration and abandons the remaining uploads, leaving the store partly uploaded.
     *
     * @param renderer  Backend that receives every owned texture.
     */
    void UploadAll(IRenderer& renderer) const;

    /// @brief Accent color for a handle's texture (invalid/empty -> @p fallback).
    [[nodiscard]] glm::vec3 SampleAccent(TextureHandle handle, glm::vec3 fallback) const;

    /// @brief Number of textures owned. Monotonically non-decreasing for the
    /// store's lifetime - nothing is ever erased.
    [[nodiscard]] std::size_t Count() const { return m_Textures.size(); }

private:
    std::unordered_map<std::string, TextureHandle> m_ByPath;  ///< Dedup index for Acquire.
    std::unordered_map<AssetId, Texture> m_Textures;          ///< The owned textures.
    /// Next id to mint (0 = invalid). Monotonic and never reused, so a handle
    /// stays valid forever and a stale handle can never alias a newer texture.
    AssetId m_NextId = 1;
};
