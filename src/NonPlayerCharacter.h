#pragma once

#include "Texture.h"
#include "GameCharacter.h"
#include "IRenderer.h"
#include "Tilemap.h"
#include "PatrolRoute.h"
#include "DialogueSystem.h"

#include <glm/glm.hpp>
#include <string>

/**
 * @class NonPlayerCharacter
 * @brief Character with patrol behavior and player interaction.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entity
 *
 * NonPlayerCharacter represents an autonomous entity in the game world.
 * NPCs follow patrol routes through the navigation map and can interact
 * with the player through collision and dialogue. Patrol routes are
 * initialized lazily during Update() (or explicitly via ReinitializePatrolRoute()).
 *
 * @see PatrolRoute, NavigationMap, PlayerCharacter
 */
class NonPlayerCharacter : public GameCharacter
{
public:
    NonPlayerCharacter();

    NonPlayerCharacter(const NonPlayerCharacter &) = delete;
    NonPlayerCharacter &operator=(const NonPlayerCharacter &) = delete;
    NonPlayerCharacter(NonPlayerCharacter &&) noexcept = default;
    NonPlayerCharacter &operator=(NonPlayerCharacter &&) noexcept = default;

    /**
     * @brief Load NPC sprite sheet from file.
     * @param relativePath Path to sprite sheet (relative to working directory).
     * @return `true` if loaded successfully.
     */
    bool Load(const std::string &relativePath);

    /**
     * @brief Upload sprite texture to the renderer.
     * @param renderer Reference to the active renderer.
     */
    void UploadTextures(IRenderer &renderer);

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
     * @param playerPosition Optional player position for collision.
     *
     * If no patrol route is currently valid, this method attempts to build one
     * from the current tile with a maximum route length of 100 waypoints.
     */
    void Update(float deltaTime, const Tilemap *tilemap, const glm::vec2 *playerPosition = nullptr);

    /**
     * @brief Render the full NPC sprite.
     * @param renderer Active renderer.
     * @param cameraPos Camera position for world-to-screen conversion.
     */
    void Render(IRenderer &renderer, glm::vec2 cameraPos) const;

    /**
     * @brief Render bottom half of sprite (for depth sorting).
     * @param renderer Active renderer.
     * @param cameraPos Camera position.
     */
    void RenderBottomHalf(IRenderer &renderer, glm::vec2 cameraPos) const;

    /**
     * @brief Render top half of sprite (for depth sorting).
     * @param renderer Active renderer.
     * @param cameraPos Camera position.
     */
    void RenderTopHalf(IRenderer &renderer, glm::vec2 cameraPos) const;

    // --- Tile accessors ---

    int GetTileX() const { return m_TileX; }
    int GetTileY() const { return m_TileY; }

    // --- Type/name/dialogue ---

    const std::string &GetType() const { return m_Type; }
    std::string GetSpritePath() const { return "assets/non-player/" + m_Type + ".png"; }

    bool IsStopped() const { return m_IsStopped; }
    void SetStopped(bool stopped) { m_IsStopped = stopped; }

    const std::string &GetName() const { return m_Name; }
    void SetName(const std::string &name) { m_Name = name; }

    const std::string &GetDialogue() const { return m_Dialogue; }
    void SetDialogue(const std::string &dialogue) { m_Dialogue = dialogue; }

    const DialogueTree& GetDialogueTree() const { return m_DialogueTree; }
    DialogueTree& GetDialogueTree() { return m_DialogueTree; }
    void SetDialogueTree(const DialogueTree& tree) { m_DialogueTree = tree; }
    bool HasDialogueTree() const { return !m_DialogueTree.nodes.empty(); }

    /**
     * @brief Reinitialize patrol route from current position.
     * @param tilemap Tilemap for navigation queries.
     * Uses maxRouteLength=100 for route generation.
     * @return `true` if valid route was created.
     */
    bool ReinitializePatrolRoute(const Tilemap *tilemap);

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

    Texture m_SpriteSheet;
    std::string m_Type;
    std::string m_Name;
    std::string m_Dialogue;
    DialogueTree m_DialogueTree;

    int m_TileX{0};
    int m_TileY{0};

    int m_TargetTileX{0};
    int m_TargetTileY{0};

    float m_WaitTimer{0.0f};
    bool m_IsStopped{false};
    bool m_StandingStill{false};
    float m_LookAroundTimer{0.0f};
    float m_RandomStandStillCheckTimer{0.0f};
    float m_RandomStandStillTimer{0.0f};

    PatrolRoute m_PatrolRoute;

    /// @}

private:
    void UpdateLookAround(float deltaTime);
    void EnterStandingStillMode(bool isRandom, float duration = 0.0f);
    void UpdateDirectionFromMovement(int dx, int dy);
    bool CheckPlayerCollision(const glm::vec2& newPosition, const glm::vec2* playerPos) const;
};
