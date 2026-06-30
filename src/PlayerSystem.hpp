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
 * @brief Free-function player appearance / mode / per-frame logic over components.
 * @author Alex (https://github.com/lextpf)
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
/**
 * @brief Switch the player to a character variant: load its walk/run/bicycle sheets
 * via the TextureStore + AssetRegistry from globals, set Appearance, and re-sample
 * the dialogue accent.
 *
 * @param world  ECS registry; the player's PlayerSprite + Appearance are updated in place.
 * @param player Player entity to restyle.
 * @param type   Character variant to load (resolved to asset paths via the AssetRegistry).
 * @return true if the walk + run sheets loaded (bicycle is optional); false on any
 *         failure, leaving the current appearance untouched.
 */
bool SwitchCharacter(ecs::registry& world, ecs::entity player, CharacterType type);

/**
 * @brief Copy an NPC look onto the player (walking sheet only); sets the disguise
 * flag and re-samples the accent.
 *
 * @param world      ECS registry; the player's PlayerSprite + Appearance are updated.
 * @param player     Player entity to disguise.
 * @param spritePath Path to the NPC's walking sheet to copy.
 * @return true if the sheet loaded; false otherwise (appearance left unchanged).
 */
bool CopyAppearanceFrom(ecs::registry& world, ecs::entity player, const std::string& spritePath);

/**
 * @brief Reload the original character sheets, clearing the disguise flag on success.
 *
 * @param world  ECS registry; delegates to @ref SwitchCharacter with the stored type.
 * @param player Player entity to restore. No-op when not currently disguised.
 */
void RestoreOriginalAppearance(ecs::registry& world, ecs::entity player);

/**
 * @brief Re-upload the player's three sheets to @p renderer (e.g. after a character
 * switch or a renderer/backend swap).
 *
 * @param world    ECS registry holding the player's PlayerSprite handles.
 * @param player   Player entity whose sheets are uploaded.
 * @param renderer Target renderer; receives the walk/run/bicycle textures. No-op when
 *                 no TextureStore is bound in globals.
 */
void UploadTextures(const ecs::registry& world, ecs::entity player, IRenderer& renderer);

/**
 * @brief Bind the player's three sheets to atlas regions; a null @p atlasTex reverts
 * to the per-player sheets. Offsets are pixel-space within the atlas.
 *
 * @param world         ECS registry; updates the player's PlayerSprite atlas fields.
 * @param player        Player entity to bind.
 * @param atlasTex      Shared atlas texture, or nullptr to revert to per-player sheets.
 * @param walkOffset    Walk sheet's pixel offset within the atlas.
 * @param runOffset     Run sheet's pixel offset within the atlas.
 * @param bicycleOffset Bicycle sheet's pixel offset within the atlas.
 */
void SetAtlasBinding(ecs::registry& world,
                     ecs::entity player,
                     const Texture* atlasTex,
                     glm::vec2 walkOffset,
                     glm::vec2 runOffset,
                     glm::vec2 bicycleOffset);

/**
 * @brief Resolve the player's walking sheet through the TextureStore in globals (a
 * shared empty texture if none is bound). Used by atlas packing and the render path.
 *
 * @param world  ECS registry supplying the TextureStore via globals.
 * @param sprite Player sprite component holding the sheet handles.
 * @return The walking sheet texture, or a shared empty texture when no store is bound.
 */
const Texture& GetSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite);

/// @brief As @ref GetSpriteSheet, for the running sheet (@c sprite.run).
const Texture& GetRunningSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite);

/// @brief As @ref GetSpriteSheet, for the bicycle sheet (@c sprite.bicycle).
const Texture& GetBicycleSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite);

/**
 * @brief Per-frame player animation + elevation advance (the former
 * PlayerCharacter::Update): velocity-driven walk cadence + smooth elevation.
 *
 * @param world     ECS registry holding the player's animation + elevation components.
 * @param player    Player entity to advance.
 * @param deltaTime Frame time in seconds.
 */
void Update(ecs::registry& world, ecs::entity player, float deltaTime);

/**
 * @brief Full per-frame movement step (the former PlayerCharacter::Move): wraps
 * @ref PlayerMovementSystem::Step over the player's components; collision runs
 * through the stateless @ref CollisionSystem free functions.
 *
 * @param world        ECS registry; the player's movement components are stepped in place.
 * @param player       Player entity to move.
 * @param direction    Desired move direction (normalized input).
 * @param deltaTime    Frame time in seconds.
 * @param tilemap      World tilemap for collision/walkability, or null to skip world blocking.
 * @param npcPositions NPC feet positions for overlap blocking, or null when absent.
 */
void Move(ecs::registry& world,
          ecs::entity player,
          glm::vec2 direction,
          float deltaTime,
          const Tilemap* tilemap,
          const std::vector<glm::vec2>* npcPositions);

/**
 * @brief Stop movement and reset to idle (the former PlayerCharacter::Stop).
 *
 * @param world  ECS registry holding the player's movement + animation components.
 * @param player Player entity to halt.
 */
void Stop(ecs::registry& world, ecs::entity player);

/**
 * @brief Snap the player feet to the bottom-center of a tile and reset the motor.
 *
 * @param world  Player-owning ECS registry.
 * @param player Player entity to reposition.
 * @param tileX  Destination tile column.
 * @param tileY  Destination tile row.
 */
void SetTilePosition(ecs::registry& world, ecs::entity player, int tileX, int tileY);

/**
 * @brief Set the player feet to an exact world position and reset the motor (no tile
 * snapping; used by dialogue alignment).
 *
 * @param world  Player-owning ECS registry.
 * @param player Player entity to reposition.
 * @param pos    Exact feet position (world space).
 */
void SetPositionRaw(ecs::registry& world, ecs::entity player, glm::vec2 pos);
}  // namespace PlayerSystem
