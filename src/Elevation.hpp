#pragma once

/**
 * @struct Elevation
 * @brief Smooth elevation transition state plus the logical collision plane.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The shared elevation component carved out of the GameCharacter base. @ref
 * offset is the smoothed visual Y shift (pixels) that interpolates toward the
 * tile elevation via smoothstep; @ref plane is the discrete logical elevation
 * the character occupies, used by collision gating. The transition logic lives
 * in the free functions in CharacterKinematics.hpp.
 *
 * Plain data struct: unprefixed @c camelCase fields. Flat aggregate so it is
 * usable directly as an ECS component (packed storage).
 *
 * @see CharacterKinematics, Transform
 */
struct Elevation
{
    float offset{0.0f};    ///< Current visual Y offset in pixels
    float target{0.0f};    ///< Target elevation to interpolate toward
    float start{0.0f};     ///< Elevation at start of the current transition
    float progress{1.0f};  ///< Interpolation progress (0 = start, 1 = done)
    int plane{0};          ///< Logical elevation plane (pixels); drives collision gating
};
