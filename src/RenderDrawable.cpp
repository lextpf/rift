#include "RenderDrawable.hpp"

#include "AnimationState.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "NpcRender.hpp"
#include "NpcSprite.hpp"
#include "PlayerModes.hpp"
#include "PlayerRender.hpp"
#include "PlayerSprite.hpp"
#include "Transform.hpp"

namespace
{
// Shared builder for an entity's bottom + top half Drawables. The only per-entity
// difference (which render components to resolve and which DrawHalf to call) is
// captured entirely in the @p drawHalf thunk, which the Y-sort/draw loop treats
// as data - so player and NPC share this body and supply only their thunk.
void AddEntityDrawables(std::vector<Drawable>& list,
                        const ecs::registry& world,
                        ecs::entity entity,
                        glm::vec2 feetPos,
                        float topOffset,
                        std::uint8_t bottomTieBias,
                        std::uint8_t topTieBias,
                        void (*drawHalf)(const Drawable&, IRenderer&, glm::vec2, bool))
{
    Drawable bottom;
    bottom.cls = DrawableClass::Entity;
    bottom.sortY = feetPos.y;
    bottom.tieBias = bottomTieBias;
    bottom.topHalf = false;
    bottom.world = &world;
    bottom.handle = entity;
    bottom.drawHalf = drawHalf;
    list.push_back(bottom);

    Drawable top;
    top.cls = DrawableClass::Entity;
    top.sortY = feetPos.y - topOffset;
    top.tieBias = topTieBias;
    top.topHalf = true;
    top.world = &world;
    top.handle = entity;
    top.drawHalf = drawHalf;
    list.push_back(top);
}
}  // namespace

void AddNpcDrawables(std::vector<Drawable>& list,
                     const ecs::registry& world,
                     ecs::entity npc,
                     glm::vec2 feetPos,
                     float topOffset,
                     std::uint8_t bottomTieBias,
                     std::uint8_t topTieBias)
{
    // Capture-less lambda -> function pointer. Re-resolves the NPC's render
    // components from (world, handle) at draw time and defers to the free
    // NpcRender::DrawHalf - no Component& is cached in the Drawable.
    void (*thunk)(const Drawable&, IRenderer&, glm::vec2, bool) =
        +[](const Drawable& d, IRenderer& r, glm::vec2 cam, bool topHalf)
    {
        auto [xf, elev, facing, anim, sprite] =
            d.world->get<Transform, Elevation, Facing, AnimationState, NpcSprite>(d.handle);
        NpcRender::DrawHalf(*d.world, r, cam, topHalf, xf, elev, facing, anim, sprite);
    };

    AddEntityDrawables(list, world, npc, feetPos, topOffset, bottomTieBias, topTieBias, thunk);
}

void AddPlayerDrawables(std::vector<Drawable>& list,
                        const ecs::registry& world,
                        ecs::entity player,
                        glm::vec2 feetPos,
                        float topOffset,
                        std::uint8_t bottomTieBias,
                        std::uint8_t topTieBias)
{
    void (*thunk)(const Drawable&, IRenderer&, glm::vec2, bool) =
        +[](const Drawable& d, IRenderer& r, glm::vec2 cam, bool topHalf)
    {
        auto [xf, elev, facing, anim, modes, sprite] =
            d.world->get<Transform, Elevation, Facing, AnimationState, PlayerModes, PlayerSprite>(
                d.handle);
        PlayerRender::DrawHalf(*d.world, r, cam, topHalf, xf, elev, facing, anim, modes, sprite);
    };

    AddEntityDrawables(list, world, player, feetPos, topOffset, bottomTieBias, topTieBias, thunk);
}
