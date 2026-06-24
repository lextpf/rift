#pragma once

#include "CharacterType.hpp"

#include <ecs.hpp>

#include <glm/glm.hpp>

#include <string>
#include <vector>

class IRenderer;
class Texture;
class Tilemap;
struct PlayerSprite;

/**
 * @file PlayerSystem.hpp
 * @brief Free-function player appearance / mode / per-frame logic over components.
 * @ingroup Entities
 *
 * The player analog of NpcAiSystem/NpcRender: the behavior carved out of the
 * former PlayerCharacter class (SwitchCharacter, CopyAppearanceFrom, sprite/atlas
 * binding, per-frame Update) as free functions over the player entity's granular
 * components. Services (TextureStore / AssetRegistry) are read from
 * @c world.globals().find<WorldServices>() instead of per-entity pointers (the
 * player's 1b). The per-frame movement step + Stop stay in @ref PlayerMovementSystem
 * (already component-based); this is the appearance/mode/animation glue.
 */
namespace PlayerSystem
{
/// @brief Switch the player to a character variant: load its walk/run/bicycle
/// sheets via the TextureStore + AssetRegistry from globals, set Appearance, and
/// re-sample the dialogue accent. @return true if walk+run loaded.
bool SwitchCharacter(ecs::registry& world, ecs::entity player, CharacterType type);

/// @brief Copy an NPC look onto the player (walking sheet only); sets the
/// disguise flag + re-samples the accent. @return true if the sheet loaded.
bool CopyAppearanceFrom(ecs::registry& world, ecs::entity player, const std::string& spritePath);

/// @brief Reload the original character sheets (clears the disguise flag on success).
void RestoreOriginalAppearance(ecs::registry& world, ecs::entity player);

/// @brief Re-upload the player's three sheets to @p renderer (e.g. after a switch).
void UploadTextures(const ecs::registry& world, ecs::entity player, IRenderer& renderer);

/// @brief Bind the player's three sheets to atlas regions (nullptr atlas reverts
/// to per-player sheets). Pixel-space offsets within the atlas.
void SetAtlasBinding(ecs::registry& world,
                     ecs::entity player,
                     const Texture* atlasTex,
                     glm::vec2 walkOffset,
                     glm::vec2 runOffset,
                     glm::vec2 bicycleOffset);

/// @brief Resolve the player's walk / run / bicycle sheet through the TextureStore
/// in globals (a shared empty texture if none is bound). Used by atlas packing
/// and the render path.
const Texture& GetSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite);
const Texture& GetRunningSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite);
const Texture& GetBicycleSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite);

/// @brief Per-frame player animation + elevation advance (the former
/// PlayerCharacter::Update): velocity-driven walk cadence + smooth elevation.
void Update(ecs::registry& world, ecs::entity player, float deltaTime);

/// @brief Full per-frame movement step (the former PlayerCharacter::Move): wraps
/// PlayerMovementSystem::Step over the player's components + CollisionResolver.
void Move(ecs::registry& world,
          ecs::entity player,
          glm::vec2 direction,
          float deltaTime,
          const Tilemap* tilemap,
          const std::vector<glm::vec2>* npcPositions);

/// @brief Stop movement + reset to idle (the former PlayerCharacter::Stop).
void Stop(ecs::registry& world, ecs::entity player);

/// @brief Snap the player feet to the bottom-center of a tile and reset the motor.
void SetTilePosition(ecs::registry& world, ecs::entity player, int tileX, int tileY);

/// @brief Set the player feet to an exact world position and reset the motor
/// (no tile snapping; used by dialogue alignment).
void SetPositionRaw(ecs::registry& world, ecs::entity player, glm::vec2 pos);
}  // namespace PlayerSystem
