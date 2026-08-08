#pragma once

#include <glm/glm.hpp>

#include <cstdint>

/**
 * @enum SupportSurface
 * @brief Logical walk surface supporting a character at a world position.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Height and surface identity are deliberately separate. Ground continues underneath authored
 * elevation, while Elevation is a ramp or deck surface occupying the same X and Y footprint.
 *
 * @see SurfaceSystem, SupportState, Elevation, ElevationAxis
 */
enum class SupportSurface : std::uint8_t
{
    /// Implicit ground plane. Present under every cell, including elevated ones, and always
    /// paired with height 0. Only collision tiles whose authored elevation is 0 can block it.
    Ground = 0,
    /// An authored ramp/deck surface, paired with that cell's exact non-zero elevation. Only
    /// collision tiles at the same exact elevation can block it.
    Elevation = 1,
};

/**
 * @struct SupportState
 * @brief Persistent topology state used by movement, collision, and rendering.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * The two fields mean nothing apart. @ref SurfaceSystem::CollisionBelongsTo matches a collision
 * cell against both, and @ref SupportSurface::Ground always carries height 0. On an entity they
 * live as Elevation::surface and Elevation::plane, and CharacterKinematics::CommitSupport is the
 * only function that may write them together.
 *
 * @see SurfaceSystem::ResolveMove, CharacterKinematics::CommitSupport
 */
struct SupportState
{
    SupportSurface surface{SupportSurface::Ground};  ///< Ground vs. authored-elevation topology.
    /**
     * @brief Height of the supporting surface, in the same pixel units as
     *        Tilemap::GetElevation (0 = ground).
     *
     * A collision cell must carry exactly this elevation to block a character standing here,
     * so a 6px ramp never snags on an adjacent 10px railing.
     */
    int height{0};

    bool operator==(const SupportState&) const = default;
};

/**
 * @struct SurfaceTransition
 * @brief Result of resolving a movement probe through the support graph.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Resolving a probe mutates nothing, so a caller may discard the result and try a different
 * candidate position.
 *
 * @see SurfaceSystem::ResolveMove, CollisionSystem::MovementProbeResult
 */
struct SurfaceTransition
{
    /**
     * @brief Support the character would stand on after the move.
     *
     * Meaningful only when @ref connected is true. A rejected probe reports the last connected
     * support reached before the failing boundary, which equals the caller's current support only
     * when the first sub-step failed.
     */
    SupportState support{};
    /**
     * @brief False means "unsupported step/drop": the support graph has no edge for this move.
     *
     * The caller must block the entire movement transaction and commit nothing - neither a
     * clamped nor a partial (single-axis) application of it.
     */
    bool connected{true};
};

/**
 * @struct CharacterCollisionBody
 * @brief Feet anchor plus exact support for character-vs-character collision.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * A per-frame snapshot rebuilt from live components, never stored on an entity. Both directions
 * of the same symmetric test produce one: EntityStore::BuildNpcCollisionBodies rebuilds the NPC
 * vector for player movement, and Game builds a single player body each frame for NPC AI.
 * Characters whose support differs never collide, which is what lets one character walk under a
 * bridge while another walks across its deck.
 *
 * @see CollisionSystem::CollidesWithNPC, NpcAiSystem
 */
struct CharacterCollisionBody
{
    glm::vec2 feet{0.0f};    ///< Bottom-center anchor in world pixels, with +Y pointing down.
    SupportState support{};  ///< Exact surface + height; must match for a collision to register.
};
