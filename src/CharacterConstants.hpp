#pragma once

/// @file CharacterConstants.hpp
/// @brief Shared character tuning constants, extracted from the GameCharacter base.
/// @ingroup Entities
///
/// These were `static constexpr` members of the former GameCharacter base. With
/// that base dissolved into ECS components, the constants live here as free
/// `constexpr` so the player, NPCs, and the systems that operate on their
/// components can reference them without a base class.

namespace CharacterConstants
{
/// @brief Sprite sheet cell width in pixels.
inline constexpr int SPRITE_WIDTH = 32;
/// @brief Sprite sheet cell height in pixels.
inline constexpr int SPRITE_HEIGHT = 32;
/// @brief Float twins of the sprite cell size (for vec2 / pixel math).
inline constexpr float SPRITE_WIDTH_F = static_cast<float>(SPRITE_WIDTH);
inline constexpr float SPRITE_HEIGHT_F = static_cast<float>(SPRITE_HEIGHT);
/// @brief Animation frames per direction in a sprite row (the 3 cells walked
/// through by @ref WALK_SEQUENCE; distinct from @ref WALK_SEQUENCE_LENGTH = 4).
inline constexpr int WALK_FRAME_COUNT = 3;
/// @brief Seconds per walk/animation frame.
inline constexpr float ANIM_FRAME_DURATION = 0.15f;
/// @brief AABB floating-point tolerance.
inline constexpr float COLLISION_EPS = 0.05f;

/// @brief Feet-anchored collision hitbox, shared by player + NPC (one tile).
inline constexpr float HITBOX_WIDTH = 16.0f;
inline constexpr float HITBOX_HEIGHT = 16.0f;
inline constexpr float HALF_HITBOX_WIDTH = HITBOX_WIDTH / 2.0f;
inline constexpr float HALF_HITBOX_HEIGHT = HITBOX_HEIGHT / 2.0f;

/// @brief Per-character base walk speeds (px/s); mode multipliers apply on top.
inline constexpr float PLAYER_BASE_SPEED = 50.0f;
inline constexpr float NPC_BASE_SPEED = 25.0f;
/// @brief Movement-mode speed multipliers (over the base walk speed).
inline constexpr float RUN_SPEED_MULTIPLIER = 1.75f;
inline constexpr float BICYCLE_SPEED_MULTIPLIER = 2.25f;

/// @brief Walk cycle frame indices (Left -> Neutral -> Right -> Neutral).
inline constexpr int WALK_SEQUENCE[4] = {1, 0, 2, 0};
/// @brief Length of @ref WALK_SEQUENCE.
inline constexpr int WALK_SEQUENCE_LENGTH = 4;
/// @brief Maximum elevation delta (pixels) the plane can change in one step.
///
/// Larger deltas are rejected by the plane-update rule so a character cannot
/// jump directly from ground onto a deck without using a ramp.
inline constexpr int MAX_STEP_HEIGHT = 8;
}  // namespace CharacterConstants
