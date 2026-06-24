#pragma once

#include "CharacterDirection.hpp"

#include <ecs.hpp>

#include <glm/glm.hpp>

class IRenderer;
class Texture;
struct Transform;
struct Elevation;
struct Facing;
struct AnimationState;
struct PlayerModes;
struct PlayerSprite;

/**
 * @file PlayerRender.hpp
 * @brief Free-function player sprite rendering over granular components.
 * @ingroup Rendering
 *
 * The player analog of NpcRender: the render mechanics carved out of the former
 * PlayerCharacter::RenderBottomHalf / RenderTopHalf / DrawHalf / ResolveRenderSheet
 * / GetSpriteCoords. Distinct from NpcRender because the player has a different
 * sprite-row mapping ({DOWN=0,UP=1,LEFT=2,RIGHT=3}) and a mode-dependent sheet
 * (bicycle > run > walk). Reads the TextureStore from globals via PlayerSystem.
 */
namespace PlayerRender
{
/// @brief Sprite-sheet UV top-left (pixels) for a frame + facing (player row map).
/// @p requiresYFlip applies the OpenGL bottom-up row inversion. (The former
/// GetSpriteCoords took an AnimationType for an idle/walk/run early-out that was
/// always satisfied; dropped here, matching NpcRender::SpriteCoords.)
glm::vec2 SpriteCoords(int frame, CharacterDirection dir, bool requiresYFlip = false);

/// @brief Select the draw sheet for the active mode (bicycle > run > walk) and,
/// when atlas-bound, fold the mode's atlas offset into @p spriteCoords.
const Texture& ResolveRenderSheet(const ecs::registry& world,
                                  const PlayerModes& modes,
                                  const PlayerSprite& sprite,
                                  glm::vec2& spriteCoords);

/// @brief Draw the top or bottom half of the player sprite for the Y-sort pass.
void DrawHalf(const ecs::registry& world,
              IRenderer& renderer,
              glm::vec2 cameraPos,
              bool topHalf,
              const Transform& xf,
              const Elevation& elev,
              const Facing& facing,
              const AnimationState& anim,
              const PlayerModes& modes,
              const PlayerSprite& sprite);
}  // namespace PlayerRender
