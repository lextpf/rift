#pragma once

#include "TextureHandle.hpp"

#include <glm/glm.hpp>

class Texture;

/**
 * @struct PlayerSprite
 * @brief Player sprite identity: walk, run and bicycle sheet handles plus atlas binding.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the player's three sheet handles into a @ref TextureStore with the shared tile-atlas
 * binding the batched render path uses. @ref atlas is a non-owning pointer to the tile atlas that
 * Tilemap owns. The atlas-pack step sets it, and it is never persisted.
 *
 * The three atlas offsets are added straight onto a sprite-cell origin, so they live in the same
 * frame: GL rows counted upward from the atlas bottom, never downward from the image top. X is
 * always 0 because PackAdditionalSheets stacks the sheets vertically and left-aligned. Every
 * offset is meaningful only while @ref atlas is non-null, and every atlas re-pack replaces them.
 */
struct PlayerSprite
{
    TextureHandle walk;     ///< Walking and idle sheet handle.
    TextureHandle run;      ///< Running sheet handle.
    TextureHandle bicycle;  ///< Bicycle sheet handle.

    const Texture* atlas{nullptr};    ///< Shared tile-atlas binding; runtime cache, not persisted.
    glm::vec2 atlasWalkOffset{0.0f};  ///< Walk sheet offset, GL rows up from the atlas bottom.
    glm::vec2 atlasRunOffset{0.0f};   ///< Run sheet offset, GL rows up from the atlas bottom.
    glm::vec2 atlasBicycleOffset{0.0f};  ///< Bicycle sheet offset, GL rows up from atlas bottom.
};
