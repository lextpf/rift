#pragma once

#include "TextureHandle.hpp"

#include <glm/glm.hpp>

class Texture;

/**
 * @struct PlayerSprite
 * @brief Player sprite identity: walk/run/bicycle sheet handles + atlas binding.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the player's three sheet handles (into a @ref TextureStore) with the
 * shared tile-atlas binding used by the batched render path. @c atlas is a
 * non-owning runtime render-cache pointer (the tile atlas, owned by Tilemap),
 * set by the atlas-pack step; it is not persisted. Flat aggregate (the raw
 * pointer is a runtime field) so it is usable directly as an ECS component.
 */
struct PlayerSprite
{
    TextureHandle walk;     ///< Walking/idle sheet handle.
    TextureHandle run;      ///< Running sheet handle.
    TextureHandle bicycle;  ///< Bicycle sheet handle.

    const Texture* atlas{nullptr};    ///< Shared tile-atlas binding (runtime cache; not persisted).
    glm::vec2 atlasWalkOffset{0.0f};  ///< Walk-sheet pixel offset within the atlas.
    glm::vec2 atlasRunOffset{0.0f};   ///< Run-sheet pixel offset within the atlas.
    glm::vec2 atlasBicycleOffset{0.0f};  ///< Bicycle-sheet pixel offset within the atlas.
};
