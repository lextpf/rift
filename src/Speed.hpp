#pragma once

/**
 * @struct Speed
 * @brief Base movement speed (pixels/second) shared by all characters.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The base walk speed carved out of the GameCharacter base's loose @c m_Speed
 * field. Mode multipliers (running/bicycle) and the dev-console speedhack are
 * applied on top by the movement logic; this is just the per-character base.
 *
 * Plain data struct: flat aggregate, usable directly as an ECS component.
 */
struct Speed
{
    float value{100.0f};  ///< Base movement speed in pixels/second.
};
