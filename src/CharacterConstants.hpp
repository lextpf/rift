#pragma once

/**
 * @brief Tuning constants shared by every character: sheet geometry, animation
 *        cadence, hitbox, speeds, walk cycle and step height.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * These are identical for the player and every NPC, so they are free `constexpr`
 * rather than per-entity component fields. How a value reaches an entity decides
 * what editing it changes:
 * - seeds - the hitbox sizes are @c Hitbox's defaults and @c EntityStore writes
 *   the base speeds onto @c Speed at spawn, so each entity keeps its own copy and
 *   a new value only reaches it on the next spawn;
 * - thresholds - @ref COLLISION_EPS and @ref MAX_STEP_HEIGHT are read straight
 *   from here by the collision, kinematics and surface systems, so a new value
 *   applies everywhere at once.
 *
 * Units are world pixels and seconds. The pixel values are calibrated for the
 * project's 16px tiles: tile size comes from ProjectManifest (@c tileWidth /
 * @c tileHeight), but nothing here derives from it, so different tiles need these
 * re-tuned rather than scaled.
 */
namespace CharacterConstants
{
/**
 * @name Sprite sheet geometry
 * @brief Cell size of one animation frame in a character sheet.
 * @{
 */
inline constexpr int SPRITE_WIDTH = 32;   ///< Sheet cell width in pixels.
inline constexpr int SPRITE_HEIGHT = 32;  ///< Sheet cell height in pixels.
/// Float twin of @ref SPRITE_WIDTH, for vec2 / pixel math without a cast at every use.
inline constexpr float SPRITE_WIDTH_F = static_cast<float>(SPRITE_WIDTH);
/// Float twin of @ref SPRITE_HEIGHT, for vec2 / pixel math without a cast at every use.
inline constexpr float SPRITE_HEIGHT_F = static_cast<float>(SPRITE_HEIGHT);
/// @}

/**
 * @brief Animation frames per direction in a sprite row (the 3 cells walked
 * through by @ref WALK_SEQUENCE; distinct from @ref WALK_SEQUENCE_LENGTH = 4).
 */
inline constexpr int WALK_FRAME_COUNT = 3;
/// @brief Seconds per walk/animation frame at the 1.0x cadence reference.
inline constexpr float ANIM_FRAME_DURATION = 0.15f;
/**
 * @brief AABB floating-point tolerance, in pixels.
 *
 * Shrinks every feet-anchored box inward on all sides before an overlap test, so two boxes
 * resting edge-to-edge do not register as colliding.
 */
inline constexpr float COLLISION_EPS = 0.05f;

/**
 * @name Collision hitbox
 * @brief Feet-anchored collision box, shared by the player and NPCs. It covers exactly one tile.
 *
 * Anchored at bottom-center: the box extends @ref HALF_HITBOX_WIDTH to each side of the feet and
 * @ref HITBOX_HEIGHT straight up. These are the defaults for the @c Hitbox component.
 * @{
 */
inline constexpr float HITBOX_WIDTH = 16.0f;                       ///< Full box width, pixels.
inline constexpr float HITBOX_HEIGHT = 16.0f;                      ///< Box height above the feet.
inline constexpr float HALF_HITBOX_WIDTH = HITBOX_WIDTH / 2.0f;    ///< Half-width; 8px each side.
inline constexpr float HALF_HITBOX_HEIGHT = HITBOX_HEIGHT / 2.0f;  ///< Half-height; 8px.
/// @}

/**
 * @name Movement speeds
 * @brief Base walk speeds (px/s) and the multipliers layered on top of them.
 *
 * The base is written onto an entity's @c Speed component at spawn. A mode multiplier and
 * then the developer-console @c speedMultiplier are applied on top per frame; the modes are
 * mutually exclusive, resolving bicycle > run > walk.
 * @{
 */
inline constexpr float PLAYER_BASE_SPEED = 50.0f;     ///< Player walk speed, px/s.
inline constexpr float NPC_BASE_SPEED = 25.0f;        ///< NPC patrol speed, px/s; half the player.
inline constexpr float RUN_SPEED_MULTIPLIER = 1.75f;  ///< Running mode, over the base speed.
inline constexpr float BICYCLE_SPEED_MULTIPLIER = 2.25f;  ///< Bicycle mode, over the base speed.
/// @}

/**
 * @name Walk cycle
 * @brief The four-step frame order a walking character cycles through.
 * @{
 */
/// Sheet column per step: left contact, neutral, right contact, neutral. Column 0 is also the
/// idle pose, which is why it appears twice.
inline constexpr int WALK_SEQUENCE[4] = {1, 0, 2, 0};
inline constexpr int WALK_SEQUENCE_LENGTH = 4;  ///< Steps in @ref WALK_SEQUENCE.
/// @}

/**
 * @brief Maximum elevation step (pixels) the support graph will connect.
 *
 * Enforced by @ref SurfaceSystem, which compares this against a different quantity for each
 * of the three edge kinds - all three matter, and only the middle one is a delta:
 * - ground -&gt; elevation: the absolute destination height, so a ramp whose low end is
 *   already higher than this cannot be stepped onto at all;
 * - elevation -&gt; elevation: the delta between the destination height and the current
 *   support height, so a ramp may climb arbitrarily far in small increments;
 * - elevation -&gt; ground: the absolute source height, so stepping off a tall deck is
 *   rejected rather than becoming a free drop.
 *
 * A rejected step is not a clamp: the whole movement transaction is blocked. This is what
 * forces a character to use a ramp instead of walking straight onto a deck.
 */
inline constexpr int MAX_STEP_HEIGHT = 8;
}  // namespace CharacterConstants
