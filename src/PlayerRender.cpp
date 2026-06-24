#include "PlayerRender.hpp"

#include "AnimationState.hpp"
#include "AnimationType.hpp"
#include "CharacterConstants.hpp"
#include "CharacterRender.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "IRenderer.hpp"
#include "PlayerModes.hpp"
#include "PlayerSprite.hpp"
#include "PlayerSystem.hpp"
#include "Texture.hpp"
#include "Transform.hpp"

glm::vec2 PlayerRender::SpriteCoords(int frame, CharacterDirection dir, bool requiresYFlip)
{
    int clampedFrame = frame % CharacterConstants::WALK_FRAME_COUNT;
    int spriteX = clampedFrame * CharacterConstants::SPRITE_WIDTH;

    // Map direction to logical row index (player row order).
    int dirRow = 0;
    switch (dir)
    {
        case CharacterDirection::DOWN:
            dirRow = 0;
            break;
        case CharacterDirection::UP:
            dirRow = 1;
            break;
        case CharacterDirection::LEFT:
            dirRow = 2;
            break;
        case CharacterDirection::RIGHT:
            dirRow = 3;
            break;
    }

    if (requiresYFlip)
    {
        // OpenGL: invert row order due to bottom-up texture coordinate system.
        static const int glRowMap[] = {2, 3, 1, 0};
        dirRow = glRowMap[dirRow];
    }

    return glm::vec2(static_cast<float>(spriteX),
                     static_cast<float>(dirRow * CharacterConstants::SPRITE_HEIGHT));
}

const Texture& PlayerRender::ResolveRenderSheet(const ecs::registry& world,
                                                const PlayerModes& modes,
                                                const PlayerSprite& sprite,
                                                glm::vec2& spriteCoords)
{
    // Select sprite sheet based on movement mode (bicycle > run > walk).
    const Texture& localSheet = modes.isBicycling
                                    ? PlayerSystem::GetBicycleSpriteSheet(world, sprite)
                                : (modes.animationType == AnimationType::RUN)
                                    ? PlayerSystem::GetRunningSpriteSheet(world, sprite)
                                    : PlayerSystem::GetSpriteSheet(world, sprite);
    // When atlas-bound, draw out of the shared atlas with the mode's baked offset.
    if (sprite.atlas != nullptr)
    {
        spriteCoords += modes.isBicycling                             ? sprite.atlasBicycleOffset
                        : (modes.animationType == AnimationType::RUN) ? sprite.atlasRunOffset
                                                                      : sprite.atlasWalkOffset;
        return *sprite.atlas;
    }
    return localSheet;
}

void PlayerRender::DrawHalf(const ecs::registry& world,
                            IRenderer& renderer,
                            glm::vec2 cameraPos,
                            bool topHalf,
                            const Transform& xf,
                            const Elevation& elev,
                            const Facing& facing,
                            const AnimationState& anim,
                            const PlayerModes& modes,
                            const PlayerSprite& sprite)
{
    glm::vec2 spriteCoords = SpriteCoords(anim.currentFrame, facing.dir, renderer.RequiresYFlip());
    const Texture& sheet = ResolveRenderSheet(world, modes, sprite, spriteCoords);
    const glm::vec2 spriteSize(CharacterConstants::SPRITE_WIDTH_F,
                               CharacterConstants::SPRITE_HEIGHT_F);
    glm::vec2 renderPos = CharacterRender::ComputeRenderPos(
        renderer, xf.position, cameraPos, elev.offset, spriteSize);
    CharacterRender::DrawPart(
        renderer,
        sheet,
        renderPos,
        spriteCoords,
        spriteSize,
        topHalf ? CharacterRender::Part::TopHalf : CharacterRender::Part::BottomHalf,
        /*suspendPerspective=*/true);
}
