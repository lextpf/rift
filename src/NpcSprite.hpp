#pragma once

#include "TextureHandle.hpp"

#include <glm/glm.hpp>

class Texture;

/**
 * @struct NpcSprite
 * @brief NPC sprite identity: sheet handle + atlas binding.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the NPC's sheet handle (into a @ref TextureStore) with the shared
 * tile-atlas binding used by the batched render path. @c atlas is a non-owning
 * runtime render-cache pointer (the tile atlas, owned by Tilemap), set by the
 * atlas-pack step; it is not persisted. Flat aggregate (the raw pointer is a
 * runtime field) so it is usable directly as an ECS component.
 */
struct NpcSprite
{
    TextureHandle sheet;            ///< Sheet handle into a TextureStore.
    const Texture* atlas{nullptr};  ///< Shared tile-atlas binding (runtime cache; not persisted).
    glm::vec2 atlasOffset{0.0f};    ///< Pixel offset of this NPC's sheet within the atlas.
    glm::vec3 accentColor{0.0f};    ///< Dialogue accent sampled eagerly when the sheet changes.
};
