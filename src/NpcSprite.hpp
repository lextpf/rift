#pragma once

#include "TextureHandle.hpp"

#include <glm/glm.hpp>

class Texture;

/**
 * @struct NpcSprite
 * @brief NPC sprite identity: sheet handle plus atlas binding.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the NPC's sheet handle into a @ref TextureStore with the shared tile-atlas binding the
 * batched render path uses. @ref atlas is a non-owning pointer to the tile atlas that Tilemap
 * owns. The atlas-pack step sets it, and it is never persisted.
 */
struct NpcSprite
{
    TextureHandle sheet;            ///< Sheet handle into a TextureStore.
    const Texture* atlas{nullptr};  ///< Shared tile-atlas binding; runtime cache, not persisted.
    glm::vec2 atlasOffset{0.0f};    ///< Pixel offset of this NPC's sheet within the atlas.
    /**
     * @brief Dialogue accent color.
     *
     * Sampled from the sheet once at spawn and never re-sampled, because no NPC sheet swap
     * exists. Falls back to ambience::DIALOGUE_ACCENT_FALLBACK when no TextureStore is published.
     */
    glm::vec3 accentColor{0.0f};
};
