#pragma once

#include "DialogueTypes.hpp"
#include "GameCharacter.hpp"
#include "IRenderer.hpp"
#include "PatrolRoute.hpp"
#include "Texture.hpp"
#include "Tilemap.hpp"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

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
     * @brief Register an NPC sprite path for a type identifier.
     * @param type NPC type identifier, usually the sprite filename without extension.
     * @param path Sprite sheet path.
     */
    static void SetNpcAsset(const std::string& type, const std::string& path);

    /**
     * @brief List all registered NPC type identifiers (asset-table keys).
     * @return Type names in unspecified order; empty until the manifest
     *         populates the table at startup. Used by the console `npc.spawn`
     *         argument autocomplete.
     */
    static std::vector<std::string> AvailableTypes();

    /**
     * @brief Resolve an NPC sprite path from a type identifier.
     * @param type NPC type identifier.
     * @return Registered path, or the legacy assets/non-player fallback path.
     */
    static std::string ResolveAssetPath(const std::string& type);

    /**
     * @brief Extract the NPC type identifier from a sprite path.
     * @param path Sprite sheet path.
     * @return Filename without .png when present.
     */
    static std::string TypeFromSpritePath(const std::string& path);

    /**
     * @brief Upload sprite texture to the renderer.
     * @param renderer Reference to the active renderer.
     */
    void UploadTextures(IRenderer& renderer);

    /**
     * @brief Bind this NPC's sprite to an atlas region.
     *
     * When bound, @ref Render / @ref RenderBottomHalf / @ref RenderTopHalf
     * draw out of @p atlasTex with UVs shifted by @p atlasOffset, instead
     * of the per-NPC @ref m_SpriteSheet. This lets all NPCs share a single
     * texture (the tile atlas) and batch into one draw call. Pass
     * @p atlasTex = nullptr to revert to the per-NPC sheet.
     */
    void SetAtlasBinding(const Texture* atlasTex, glm::vec2 atlasOffset);

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
    std::string GetSpritePath() const { return ResolveAssetPath(m_Type); }

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

    /// @brief Get NPC's current target tile column (next waypoint).
    int GetTargetTileX() const { return m_TargetTileX; }
    /// @brief Get NPC's current target tile row (next waypoint).
    int GetTargetTileY() const { return m_TargetTileY; }

    /// @brief Get read-only reference to the patrol route.
    const PatrolRoute& GetPatrolRoute() const { return m_PatrolRoute; }
    /// @brief Get mutable reference to the patrol route (for editor recalculation).
    PatrolRoute& GetPatrolRoute() { return m_PatrolRoute; }

    /// @brief Get the sprite sheet texture (const, for rendering).
    const Texture& GetSpriteSheet() const { return m_SpriteSheet; }
    /// @brief Get mutable reference to sprite sheet (for texture upload).
    Texture& GetSpriteSheet() { return m_SpriteSheet; }

    /**
     * @brief Get a representative accent color for this NPC (lazy, cached).
     *
     * Sampled from the sprite sheet via @ref Texture::SampleDominantNonSkinColor.
     * Used by the dialogue panel to tint the speaker ribbon and selected-option
     * indicator so each NPC reads as visually distinct without authored palette
     * data. First call computes; subsequent calls return the cached value.
     *
     * Cache invalidates implicitly only when the NPC's sprite is reloaded via
     * Load(); the m_AccentSampled flag is reset there.
     *
     * @return RGB accent color in [0, 1] per channel.
     */
    glm::vec3 GetAccentColor() const;

private:
    Texture m_SpriteSheet;                   ///< Loaded sprite sheet texture
    mutable glm::vec3 m_AccentColor{0.0f};   ///< Cached accent color (lazy via GetAccentColor)
    mutable bool m_AccentSampled{false};     ///< False until GetAccentColor() runs once
    const Texture* m_AtlasTexture{nullptr};  ///< Atlas texture override (see SetAtlasBinding)
    glm::vec2 m_AtlasOffset{0.0f};           ///< Pixel offset of this NPC's sheet within the atlas
    std::string m_Type;                      ///< NPC type identifier (e.g., "elder", "guard")
    std::string m_Name;                      ///< Display name shown during dialogue
    std::string m_Dialogue;                  ///< Simple dialogue text (fallback when no tree)
    DialogueTree m_DialogueTree;             ///< Branching dialogue tree (may be empty)

    static std::unordered_map<std::string, std::string> s_NpcAssets;

    int m_TileX{0};  ///< Current tile column
    int m_TileY{0};  ///< Current tile row

    int m_TargetTileX{0};  ///< Next waypoint tile column
    int m_TargetTileY{0};  ///< Next waypoint tile row

    /// @par Idle behavior timing
    /// At each waypoint the NPC may pause and look around. The four timers
    /// below drive a small random schedule: every 5 - 10 s the NPC rolls a
    /// 30 % chance to enter a 2 - 5 s standing-still state. While standing
    /// still it triggers look-around animations on the same cadence. Concrete
    /// constants live in `NonPlayerCharacter.cpp`; the header documents the
    /// shape so designers can predict NPC behavior without reading the impl.
    float m_WaitTimer{0.0f};  ///< Countdown before resuming patrol after reaching waypoint
    bool m_IsStopped{false};  ///< Soft-collision halt while overlapping the player; reset when the
                              ///< player moves away
    bool m_StandingStill{false};    ///< Currently in the random standing-still idle state
    float m_LookAroundTimer{0.0f};  ///< Countdown until the next look-around animation step
    float m_RandomStandStillCheckTimer{
        0.0f};  ///< Interval (~5 - 10 s) between standing-still chance rolls
    float m_RandomStandStillTimer{
        0.0f};  ///< Remaining time (~2 - 5 s) in the current standing-still state

    PatrolRoute m_PatrolRoute;  ///< Generated patrol waypoints through navigation map

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
