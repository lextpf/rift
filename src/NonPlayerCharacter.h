#pragma once

#include "DialogueTypes.h"
#include "GameCharacter.h"
#include "IRenderer.h"
#include "PatrolRoute.h"
#include "Texture.h"
#include "Tilemap.h"

#include <glm/glm.hpp>
#include <string>

/**
 * @class NonPlayerCharacter
 * @brief Character with patrol behavior and player interaction.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * NonPlayerCharacter represents an autonomous entity in the game world.
 * NPCs follow patrol routes through the navigation map and can interact
 * with the player through collision and dialogue. Patrol routes are
 * initialized lazily during Update() (or explicitly via ReinitializePatrolRoute()).
 *
 * @par AI State Machine
 * NPC behavior follows a simple state machine:
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     classDef move fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef idle fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *
 *     state "Walking" as Walk:::move
 *     state "Waiting" as Wait:::idle
 *     state "Looking Around" as Look:::idle
 *     state "Standing Still" as Stand:::idle
 *
 *     [*] --> Walk: Patrol route assigned
 *     Walk --> Wait: Reached waypoint
 *     Wait --> Walk: Wait timer expired
 *     Wait --> Look: Random chance
 *     Look --> Walk: Look timer expired
 *     Walk --> Stand: Random chance
 *     Stand --> Walk: Stand timer expired
 * </pre>
 * @endhtmlonly
 *
 * @par Movement
 * NPCs move at 25 px/s (configurable via SetSpeed) along their patrol route.
 * Movement is tile-aligned - NPCs walk in cardinal directions to the center
 * of each target tile before advancing to the next waypoint.
 *
 * @see PatrolRoute, NavigationMap, PlayerCharacter
 */
class NonPlayerCharacter : public GameCharacter
{
public:
    NonPlayerCharacter();

    NonPlayerCharacter(const NonPlayerCharacter&) = delete;
    NonPlayerCharacter& operator=(const NonPlayerCharacter&) = delete;
    NonPlayerCharacter(NonPlayerCharacter&&) noexcept = default;
    NonPlayerCharacter& operator=(NonPlayerCharacter&&) noexcept = default;

    /**
     * @brief Load NPC sprite sheet from file.
     * @param relativePath Path to sprite sheet (relative to working directory).
     * @return `true` if loaded successfully.
     */
    bool Load(const std::string& relativePath);

    /**
     * @brief Upload sprite texture to the renderer.
     * @param renderer Reference to the active renderer.
     */
    void UploadTextures(IRenderer& renderer);

    /**
     * @brief Set NPC position by tile coordinates.
     * @param tileX Tile column.
     * @param tileY Tile row.
     * @param tileSize Tile size in pixels (typically 16).
     * @param preservePatrolRoute If true, keeps the current patrol route instead of resetting it.
     */
    void SetTilePosition(int tileX, int tileY, int tileSize, bool preservePatrolRoute = false);

    /**
     * @brief Update NPC AI and animation.
     * @param deltaTime Frame time in seconds.
     * @param tilemap Tilemap for navigation queries.
     * @param playerPosition Player feet position for collision checking,
     *        or `nullptr` to skip collision with the player.
     *
     * If no patrol route is currently valid, this method attempts to build one
     * from the current tile with a maximum route length of 100 waypoints.
     */
    void Update(float deltaTime, const Tilemap* tilemap, const glm::vec2* playerPosition = nullptr);

    /**
     * @brief Render the full NPC sprite.
     * @param renderer Active renderer.
     * @param cameraPos Camera position for world-to-screen conversion.
     */
    void Render(IRenderer& renderer, glm::vec2 cameraPos) const;

    /**
     * @brief Render bottom half of sprite (for depth sorting).
     * @param renderer Active renderer.
     * @param cameraPos Camera position.
     */
    void RenderBottomHalf(IRenderer& renderer, glm::vec2 cameraPos) const;

    /**
     * @brief Render top half of sprite (for depth sorting).
     * @param renderer Active renderer.
     * @param cameraPos Camera position.
     */
    void RenderTopHalf(IRenderer& renderer, glm::vec2 cameraPos) const;

    /// @name Tile Accessors
    /// @{

    /// @brief Get NPC's current tile column.
    int GetTileX() const { return m_TileX; }
    /// @brief Get NPC's current tile row.
    int GetTileY() const { return m_TileY; }

    /// @}

    /// @name Type, Name, and Dialogue
    /// @{

    /// @brief Get NPC type identifier (used for sprite path lookup).
    const std::string& GetType() const { return m_Type; }
    /// @brief Get path to NPC's sprite sheet asset.
    std::string GetSpritePath() const { return "assets/non-player/" + m_Type + ".png"; }

    /// @brief Check if NPC movement is halted.
    bool IsStopped() const { return m_IsStopped; }
    /// @brief Halt or resume NPC patrol movement.
    void SetStopped(bool stopped) { m_IsStopped = stopped; }

    /// @brief Get NPC display name.
    const std::string& GetName() const { return m_Name; }
    /// @brief Set NPC display name.
    void SetName(const std::string& name) { m_Name = name; }

    /// @brief Get simple dialogue text (non-tree fallback).
    const std::string& GetDialogue() const { return m_Dialogue; }
    /// @brief Set simple dialogue text.
    void SetDialogue(const std::string& dialogue) { m_Dialogue = dialogue; }

    /// @brief Get branching dialogue tree (const).
    const DialogueTree& GetDialogueTree() const { return m_DialogueTree; }
    /// @brief Get branching dialogue tree (mutable).
    DialogueTree& GetDialogueTree() { return m_DialogueTree; }
    /// @brief Assign a branching dialogue tree.
    void SetDialogueTree(const DialogueTree& tree) { m_DialogueTree = tree; }
    /// @brief Check if NPC has a branching dialogue tree.
    bool HasDialogueTree() const { return !m_DialogueTree.nodes.empty(); }

    /// @}

    /**
     * @brief Reinitialize patrol route from current position.
     * @param tilemap Tilemap for navigation queries.
     * Uses maxRouteLength=100 for route generation.
     * @return `true` if valid route was created.
     */
    bool ReinitializePatrolRoute(const Tilemap* tilemap);

    /**
     * @brief Reset animation to idle frame.
     */
    void ResetAnimationToIdle();

    /**
     * @brief Calculate sprite sheet UV coordinates.
     * @param frame Animation frame (0-2).
     * @param dir Facing direction.
     * @return Sprite top-left corner in pixels.
     */
    glm::vec2 GetSpriteCoords(int frame, NPCDirection dir) const;

    /// @name Public State (for editor/debug access)
    /// @{

    Texture m_SpriteSheet;        ///< Loaded sprite sheet texture
    std::string m_Type;           ///< NPC type identifier (e.g., "elder", "guard")
    std::string m_Name;           ///< Display name shown during dialogue
    std::string m_Dialogue;       ///< Simple dialogue text (fallback when no tree)
    DialogueTree m_DialogueTree;  ///< Branching dialogue tree (may be empty)

    int m_TileX{0};  ///< Current tile column
    int m_TileY{0};  ///< Current tile row

    int m_TargetTileX{0};  ///< Next waypoint tile column
    int m_TargetTileY{0};  ///< Next waypoint tile row

    float m_WaitTimer{0.0f};        ///< Countdown before resuming patrol after reaching waypoint
    bool m_IsStopped{false};        ///< Movement halted (e.g., during dialogue)
    bool m_StandingStill{false};    ///< In random standing-still idle state
    float m_LookAroundTimer{0.0f};  ///< Countdown for look-around animation
    float m_RandomStandStillCheckTimer{0.0f};  ///< Interval timer for random stand-still checks
    float m_RandomStandStillTimer{0.0f};       ///< Remaining time in random standing-still state

    PatrolRoute m_PatrolRoute;  ///< Generated patrol waypoints through navigation map

    /// @}

private:
    /// @brief Cycle through random facing directions during idle.
    /// @param deltaTime Frame time in seconds.
    void UpdateLookAround(float deltaTime);

    /// @brief Transition to standing-still idle state.
    /// @param isRandom True if triggered randomly, false if scripted.
    /// @param duration Override duration in seconds (0 = use random default).
    void EnterStandingStillMode(bool isRandom, float duration = 0.0f);

    /// @brief Set facing direction from tile movement delta.
    /// @param dx Tile movement X (-1, 0, +1).
    /// @param dy Tile movement Y (-1, 0, +1).
    void UpdateDirectionFromMovement(int dx, int dy);

    /// @brief Check if moving to a position would collide with the player.
    /// @param newPosition Proposed NPC feet position.
    /// @param playerPos Player feet position (nullable).
    bool CheckPlayerCollision(const glm::vec2& newPosition, const glm::vec2* playerPos) const;
};
