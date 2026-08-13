#include "PlayerRender.hpp"

#include "AnimationState.hpp"
#include "AnimationType.hpp"
#include "CameraFacing.hpp"
#include "CharacterConstants.hpp"
#include "CharacterRender.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "IRenderer.hpp"
#include "PlayerModes.hpp"
#include "PlayerSprite.hpp"
#include "PlayerSystem.hpp"
#include "SceneMath.hpp"
#include "Texture.hpp"
#include "Transform.hpp"

#include <algorithm>

// Column = walk frame (wrapped to the walk count); row = facing, via the logical
// order {DOWN=0, UP=1, LEFT=2, RIGHT=3} then the GL permutation below.
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
        // Logical row -> physical GL row (rows counted up from the bottom of the
        // stbi-flipped sheet). This is a permutation, not the `3 - row` inversion
        // a pure bottom-up flip would give: DOWN and UP come out swapped relative
        // to that, because the artwork's row order is not the logical order.
        //
        //   logical  DOWN(0)  UP(1)  LEFT(2)  RIGHT(3)
        //   GL row      2       3       1        0
        //
        // The result matches the row order NpcRender::SpriteCoords hard-codes
        // (RIGHT=0, LEFT=1, DOWN=2, UP=3) - both sheet families share a layout.
        static const int glRowMap[] = {2, 3, 1, 0};
        dirRow = glRowMap[dirRow];
    }

    return glm::vec2(static_cast<float>(spriteX),
                     static_cast<float>(dirRow * CharacterConstants::SPRITE_HEIGHT));
}

// Pick the sheet the current movement mode draws from, and - when the player is
// atlas-bound - shift spriteCoords by that mode's baked atlas offset instead. Mode
// priority is bicycle > run > walk; the local sheet is still resolved on the atlas
// path because the ternary is evaluated first, which is harmless (a store lookup).
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

// Pipeline: pick UVs (SpriteCoords, with the renderer's flip convention) -> resolve
// sheet + fold the mode's atlas offset (ResolveRenderSheet) -> place with elevation
// (ComputeRenderPos) -> draw the requested half (DrawPart).
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
    glm::vec2 renderPos =
        CharacterRender::ComputeRenderPos(xf.position, cameraPos, elev.offset, spriteSize);
    CharacterRender::DrawPart(
        renderer,
        sheet,
        renderPos,
        spriteCoords,
        spriteSize,
        topHalf ? CharacterRender::Part::TopHalf : CharacterRender::Part::BottomHalf);
}

// 3D pipeline: same sheet/UV resolution as DrawHalf, but the anchor stays in
// world space and the elevation lift becomes a real height instead of a
// pre-projection screen nudge - so a character on a ramp is genuinely higher in
// the scene rather than merely drawn higher on screen.
void PlayerRender::Draw3D(const ecs::registry& world,
                          IRenderer& renderer,
                          const billboard::Orientation& orientation,
                          const Transform& xf,
                          const Elevation& elev,
                          const Facing& facing,
                          const AnimationState& anim,
                          const PlayerModes& modes,
                          const PlayerSprite& sprite)
{
    // Sheet rows are screen directions, not world ones, so the row is chosen
    // from the facing rotated into the camera's frame - otherwise orbiting
    // behind a character would still show their front. The billboard's own yaw
    // is used rather than the raw camera yaw so the row always agrees with how
    // far the quad actually turned. The stored Facing is left alone; only this
    // lookup rotates.
    const CharacterDirection screenDir =
        cameraFacing::ScreenFacing(facing.dir, orientation.yawRadians);

    glm::vec2 spriteCoords = SpriteCoords(anim.currentFrame, screenDir, renderer.RequiresYFlip());
    const Texture& sheet = ResolveRenderSheet(world, modes, sprite, spriteCoords);
    const glm::vec2 spriteSize(CharacterConstants::SPRITE_WIDTH_F,
                               CharacterConstants::SPRITE_HEIGHT_F);

    // Elevation::offset only - not the plane index. The flat path lifts the
    // sprite by exactly this (ComputeRenderPos subtracts it in Y-down screen
    // space), and the plane is a logical collision/sorting value that was never
    // a render offset.
    const glm::vec3 footCenter = sceneMath::ToScene(xf.position, elev.offset);

    CharacterRender::DrawBillboard(
        renderer, sheet, footCenter, spriteCoords, spriteSize, orientation);
}
