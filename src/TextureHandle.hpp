#pragma once

#include <cstdint>

/**
 * @brief Asset id type used as a @ref TextureStore key.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Minted monotonically by the store and never recycled, so an id identifies one
 * texture for the whole process lifetime.
 */
using AssetId = std::uint32_t;

/**
 * @struct TextureHandle
 * @brief A trivially-copyable reference to a texture owned by a @ref TextureStore.
 * @ingroup Rendering
 *
 * Replaces an owned @ref Texture member on an entity: the store owns the GPU
 * resource, and the sprite components (@ref PlayerSprite, @ref NpcSprite) hold only
 * this handle. Flat aggregate (one field, no ctors) so it is usable directly inside a
 * reflectable ECS component. @c id 0 means "no texture".
 *
 * A handle is meaningful only against the store that minted it; ids from two
 * different TextureStore instances are not interchangeable.
 */
struct TextureHandle
{
    AssetId id = 0;  ///< Store key; 0 = invalid / no texture.
};

/// @brief Identity comparison on the raw id; does not consult any store.
inline bool operator==(TextureHandle a, TextureHandle b) noexcept
{
    return a.id == b.id;
}
/// @brief Negation of the id comparison.
inline bool operator!=(TextureHandle a, TextureHandle b) noexcept
{
    return a.id != b.id;
}
