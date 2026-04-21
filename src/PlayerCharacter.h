#pragma once

#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>
#include "CollisionResolver.h"
#include "EnumTraits.h"
#include "GameCharacter.h"
#include "IRenderer.h"
#include "Texture.h"

/**
 * @enum AnimationType
 * @brief Animation state machine states.
 * @author Alex (https://github.com/lextpf)
 *
 * Determines which sprite sheet to use and animation timing:
 * - IDLE: Standing still, uses walking sheet frame 0
 * - WALK: Walking animation at base speed
 * - RUN: Running animation at 50% of normal frame duration (2x faster)
 */
enum class AnimationType
{
    IDLE = 0,  ///< Standing still (single frame)
    WALK = 1,  ///< Walking animation (4-step sequence [1,0,2,0])
    RUN = 2    ///< Running/sprinting animation (same sequence, faster timing)
};

/**
 * @enum CharacterType
 * @brief Available player character sprite variants.
 * @author Alex (https://github.com/lextpf)
 *
 * Each character type has its own set of sprite sheets:
 * - Walking sprite sheet (idle + walk animations)
 * - Running sprite sheet (sprint animation)
 * - Bicycle sprite sheet (cycling animation)
 */
enum class CharacterType
{
    BW1_MALE = 0,    ///< Black & White 1 Male protagonist
    BW1_FEMALE = 1,  ///< Black & White 1 Female protagonist
    BW2_MALE = 2,    ///< Black & White 2 Male protagonist
    BW2_FEMALE = 3,  ///< Black & White 2 Female protagonist
    CC_FEMALE = 4    ///< Crystal Clear Female character
};

/// Compile-time reflection for CharacterType.
template <>
struct EnumTraits<CharacterType> : EnumTraitsBase<CharacterType, EnumTraits<CharacterType>>
{
    static constexpr size_t Count = 5;
    static constexpr std::string_view Names[] = {
        "BW1_MALE", "BW1_FEMALE", "BW2_MALE", "BW2_FEMALE", "CC_FEMALE"};

    static_assert(std::to_underlying(CharacterType::CC_FEMALE) == Count - 1,
                  "Update EnumTraits<CharacterType> when adding new CharacterType values");
};

/**
 * @class PlayerCharacter
 * @brief Player-controlled character with movement, animation, and collision.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * @par Position (Bottom-Center)
 * Position is the bottom-center of the sprite.
 *
 * @par Hitbox
 * 16x16 pixels, extending upward from the bottom-center:
 * @code
 *     +---+---+---+
 *     |   |   |   |
 *     +---+===+---+   === = hitbox (16x16)
 *     |   |===|   |
 *     +---+-o-+---+   o = position (bottom-center)
 * @endcode
 *
 * @par Movement
 * - Walking: 80 px/s (1.0x base)
 * - Running: 152 px/s (1.9x)
 * - Bicycle: 160 px/s (2.0x)
 *
 * @par Collision
 * - Strict mode: Full 16x16 hitbox check
 * - Center mode: Center-point only, allows corner cutting
 * - Corner cutting: Automatic sliding around obstacles
 * - Lane snapping: Aligns to tile centers during cardinal movement
 *
 * @par Animation
 * Walk cycle: [1, 0, 2, 0] at 0.15s/frame (walk) or 0.075s/frame (run)
 */
class PlayerCharacter : public GameCharacter
{
public:
    /**
     * @brief Construct a new PlayerCharacter with default values.
     *
     * Initializes the player at position (200, 150) facing down.
     * No sprite sheets are loaded; call LoadSpriteSheet() before rendering.
     */
    PlayerCharacter();
    ~PlayerCharacter();

    PlayerCharacter(const PlayerCharacter&) = delete;
    PlayerCharacter& operator=(const PlayerCharacter&) = delete;
    PlayerCharacter(PlayerCharacter&& other) noexcept;
    PlayerCharacter& operator=(PlayerCharacter&& other) noexcept;

    /**
     * @brief Load the walking/idle sprite sheet.
     *
     * The sprite sheet should be a 4-row x 3-column grid of 32x32 sprites:
     * @code
     * +----+----+----+
     * | D0 | D1 | D2 |  Row 0: Down
     * +----+----+----+
     * | U0 | U1 | U2 |  Row 1: Up
     * +----+----+----+
     * | L0 | L1 | L2 |  Row 2: Left
     * +----+----+----+
     * | R0 | R1 | R2 |  Row 3: Right
     * +----+----+----+
     * @endcode
     *
     * @param path Path to the sprite sheet PNG file.
     * @return true if loaded successfully, false on error.
     */
    bool LoadSpriteSheet(const std::string& path);

    /**
     * @brief Load the running sprite sheet.
     *
     * Same format as walking sprite sheet but with running poses.
     * Required for running/sprinting animations.
     *
     * @param path Path to the running sprite sheet PNG file.
     * @return true if loaded successfully, false on error.
     */
    bool LoadRunningSpriteSheet(const std::string& path);

    /**
     * @brief Load the bicycle sprite sheet.
     *
     * Same format as walking sprite sheet but with cycling poses.
     * Must be loaded before enabling bicycle mode.
     *
     * @param path Path to the bicycle sprite sheet PNG file.
     * @return true if loaded successfully, false on error.
     */
    bool LoadBicycleSpriteSheet(const std::string& path);

    /**
     * @brief Upload all sprite textures to the renderer.
     *
     * Called when switching renderers to ensure textures are properly
     * recreated in the new graphics context.
     *
     * @param renderer Reference to the active renderer.
     */
    void UploadTextures(IRenderer& renderer);

    /**
     * @brief Update player animation state.
     *
     * Advances the animation timer and updates the current frame.
     * Should be called once per frame, typically from Game::Update().
     *
     * @param deltaTime Time elapsed since last update in seconds.
     */
    void Update(float deltaTime);

    /**
     * @brief Render the player sprite at current position.
     *
     * @param renderer Reference to the active renderer.
     * @param cameraPos Current camera position in world space.
     */
    void Render(IRenderer& renderer, glm::vec2 cameraPos);

    /**
     * @brief Render only the bottom half of the player sprite (feet area).
     *
     * @param renderer Reference to the active renderer.
     * @param cameraPos Current camera position in world space.
     */
    void RenderBottomHalf(IRenderer& renderer, glm::vec2 cameraPos);

    /**
     * @brief Render only the top half of the player sprite (head/torso area).
     *
     * @param renderer Reference to the active renderer.
     * @param cameraPos Current camera position in world space.
     */
    void RenderTopHalf(IRenderer& renderer, glm::vec2 cameraPos);

    /**
     * @brief Process movement input and update position.
     *
     * @param direction Input direction vector (should be normalized or zero).
     * @param deltaTime Time elapsed since last frame in seconds.
     * @param tilemap Optional tilemap for collision detection.
     * @param npcPositions Optional list of NPC feet positions for collision.
     */
    void Move(glm::vec2 direction,
              float deltaTime,
              const class Tilemap* tilemap = nullptr,
              const std::vector<glm::vec2>* npcPositions = nullptr);

    /**
     * @brief Immediately stop movement and reset to idle animation.
     */
    void Stop();

    /**
     * @brief Enable or disable running mode.
     * @param running true to enable running, false for walking.
     */
    void SetRunning(bool running);

    /**
     * @brief Enable or disable bicycle mode.
     * @param bicycling true to enable bicycle, false to disable.
     */
    void SetBicycling(bool bicycling);

    /**
     * @brief Check if player is currently on bicycle.
     * @return true if bicycle mode is active.
     */
    bool IsBicycling() const { return m_IsBicycling; }

    /**
     * @brief Check if player is currently moving.
     * @return true if player is in motion, false if idle.
     */
    bool IsMoving() const { return m_IsMoving; }

    /**
     * @brief Switch to a different character appearance.
     * @param characterType The character type to switch to.
     * @return true if all required sprites loaded successfully.
     */
    bool SwitchCharacter(CharacterType characterType);

    /**
     * @brief Get the current character type.
     * @return The active CharacterType.
     */
    CharacterType GetCharacterType() const { return m_CharacterType; }

    /**
     * @brief Copy appearance from an NPC sprite sheet.
     * @param spritePath Path to the NPC sprite sheet.
     * @return true if sprite loaded successfully.
     */
    bool CopyAppearanceFrom(const std::string& spritePath);

    /**
     * @brief Restore original character appearance.
     */
    void RestoreOriginalAppearance();

    /**
     * @brief Check if player is using a copied NPC appearance.
     * @return true if player has transformed into an NPC appearance.
     */
    bool IsUsingCopiedAppearance() const { return m_IsUsingCopiedAppearance; }

    /**
     * @brief Set player position with tile snapping.
     *
     * Converts the input position to tile coordinates and snaps
     * the player feet to the bottom-center of that tile.
     *
     * @param pos Desired position (will be snapped to tile center).
     */
    inline void SetPosition(glm::vec2 pos) override
    {
        int tileX = static_cast<int>(std::floor(pos.x / TILE_SIZE));
        int tileY = static_cast<int>(std::floor(pos.y / TILE_SIZE));
        m_Position.x = tileX * TILE_SIZE + TILE_SIZE / 2.0f;  // horizontal center of tile
        m_Position.y = tileY * TILE_SIZE + TILE_SIZE;         // bottom of tile (feet position)
    }

    /**
     * @brief Set player position directly in world space (no tile snapping).
     *
     * Used for short interpolation phases such as dialogue alignment.
     *
     * @param pos Exact world-space feet position.
     */
    inline void SetPositionRaw(glm::vec2 pos) { m_Position = pos; }

    /**
     * @brief Set player position directly by tile coordinates.
     *
     * @param tileX Tile column (0-based).
     * @param tileY Tile row (0-based).
     */
    inline void SetTilePosition(int tileX, int tileY)
    {
        m_Position.x = tileX * TILE_SIZE + TILE_SIZE / 2.0f;  // horizontal center of tile
        m_Position.y = tileY * TILE_SIZE + TILE_SIZE;         // bottom of tile (feet position)
    }

    /**
     * @brief Get the canonical feet position for the current tile.
     * @param tileSize Size of tiles in pixels (default: 16).
     * @return Canonical feet position for the current tile.
     */
    glm::vec2 GetCurrentTileCenter(float tileSize = 16.0f) const;

    /**
     * @brief Register a custom asset path for a character sprite.
     * @param characterType The character type this asset belongs to.
     * @param spriteType One of: "Walking", "Running", or "Bicycle".
     * @param path Full path to the asset file.
     */
    static void SetCharacterAsset(CharacterType characterType,
                                  const std::string& spriteType,
                                  const std::string& path);

    /**
     * @name Render Constants
     * @{
     */
    static constexpr int RENDER_WIDTH = 16;   ///< Sprite width in pixels (1 tile wide)
    static constexpr int RENDER_HEIGHT = 32;  ///< Sprite height in pixels (2 tiles tall)
    /** @} */

    /**
     * @name Tile Constants
     * @{
     */
    static constexpr float TILE_SIZE = 16.0f;  ///< Tile size in pixels
    /** @} */

    /**
     * @name Collision Constants
     * @{
     */
    static constexpr float HITBOX_WIDTH = 16.0f;   ///< Collision box width (1 tile)
    static constexpr float HITBOX_HEIGHT = 16.0f;  ///< Collision box height (1 tile)

    static constexpr float HALF_HITBOX_WIDTH =
        HITBOX_WIDTH / 2;  ///< Half of the collision box width
    static constexpr float HALF_HITBOX_HEIGHT =
        HITBOX_HEIGHT / 2;  ///< Half of the collision box height
    /** @} */

    /// @brief Access to the underlying collision resolver (for tests and rare
    /// callers that need the collision pipeline outside of Move()).
    CollisionResolver& GetCollision() { return m_Collision; }
    const CollisionResolver& GetCollision() const { return m_Collision; }

private:
    /**
     * @name Sprite Sheet Textures
     * @{
     */
    Texture m_SpriteSheet;         ///< Walking/idle sprite sheet
    Texture m_RunningSpriteSheet;  ///< Running sprite sheet
    Texture m_BicycleSpriteSheet;  ///< Bicycle sprite sheet
    /** @} */

    /**
     * @name Movement State
     * @{
     */
    bool m_IsRunning;                   ///< Running mode flag (1.9x speed)
    bool m_IsBicycling;                 ///< Bicycle mode flag (2.0x speed)
    bool m_IsUsingCopiedAppearance;     ///< True if using copied NPC appearance
    glm::vec2 m_LastSafeTileCenter;     ///< Last valid tile center (for stuck recovery)
    glm::vec2 m_LastMovementDirection;  ///< Previous frame's movement direction
    glm::vec2 m_SlideHysteresisDir;     ///< Last chosen slide dir to avoid jitter
    float m_SlideCommitTimer;           ///< Time remaining before slide direction can change
    int m_AxisPreference;               ///< -1 = prefer Y, +1 = prefer X, 0 = no preference
    float m_AxisCommitTimer;            ///< Time remaining before axis preference can change
    glm::vec2 m_SnapStartPos;           ///< Position when idle snap began
    glm::vec2 m_SnapTargetPos;          ///< Target position for idle snap
    float m_SnapProgress;               ///< Progress 0->1 for smoothstep snap
    bool m_IsMoving;                    ///< True if currently in motion
    int m_LastInputX = 0;               ///< Last non-zero A/D sign: -1 left, +1 right
    int m_LastInputY = 0;               ///< Last non-zero W/S sign: -1 up, +1 down
    /** @} */

    /**
     * @name Animation State
     * @{
     */
    AnimationType m_AnimationType;  ///< Current animation type (sprite sheet)
    CharacterType m_CharacterType;  ///< Current character appearance
    /** @} */

    /**
     * @name Sprite Sheet Layout Constants
     * @{
     */
    static constexpr int SPRITES_PER_ROW =
        3;                                 ///< Sprites per row in sheet (3 frames per direction)
    static constexpr int IDLE_FRAMES = 3;  ///< Frames in idle animation
    static constexpr int WALK_FRAMES = 3;  ///< Frames in walk animation
    static const float ANIMATION_SPEED;    ///< Base frame duration (0.15s)
    /** @} */

    /**
     * @brief Calculate sprite sheet UV coordinates for a given animation frame.
     * @param frame Animation frame index (0-2).
     * @param dir Facing direction.
     * @param anim Animation type (idle, walk, run).
     * @param requiresYFlip Whether to flip Y coordinates for the renderer.
     * @return Top-left corner of the sprite region in pixel coordinates.
     */
    glm::vec2 GetSpriteCoords(int frame,
                              Direction dir,
                              AnimationType anim,
                              bool requiresYFlip = false);

    friend class CollisionResolver;
    CollisionResolver m_Collision;  ///< Handles collision detection and resolution

    /// @brief Key for the character asset lookup table.
    struct CharacterAssetKey
    {
        CharacterType type;
        std::string spriteType;

        bool operator<(const CharacterAssetKey& other) const
        {
            if (type != other.type)
                return type < other.type;
            return spriteType < other.spriteType;
        }
    };

    static std::map<CharacterAssetKey, std::string>
        s_CharacterAssets;  ///< Custom asset paths keyed by (CharacterType, spriteType)
};
