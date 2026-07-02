#pragma once

#include <cstdint>

/**
 * @brief Lightweight handle into a @ref TextureStore.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 */

/// @brief Asset id type used as a @ref TextureStore key.
using AssetId = std::uint32_t;

/**
 * @struct TextureHandle
 * @brief A trivially-copyable reference to a texture owned by a @ref TextureStore.
 * @ingroup Rendering
 *
 * Replaces an owned @ref Texture member on an entity: the store owns the GPU
 * resource, the entity (and, later, an ECS @c Sprite component) holds only this
 * handle. Flat aggregate (one field, no ctors) so it is usable directly inside a
 * reflectable ECS component. @c id 0 means "no texture".
 */
struct TextureHandle
{
    AssetId id = 0;  ///< Store key; 0 = invalid / no texture.
};

inline bool operator==(TextureHandle a, TextureHandle b) noexcept
{
    return a.id == b.id;
}
inline bool operator!=(TextureHandle a, TextureHandle b) noexcept
{
    return a.id != b.id;
}
