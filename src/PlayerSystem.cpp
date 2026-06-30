#include "PlayerSystem.hpp"

#include "AmbienceConfig.hpp"
#include "AnimationState.hpp"
#include "Appearance.hpp"
#include "AssetRegistry.hpp"
#include "CharacterConstants.hpp"
#include "CharacterKinematics.hpp"
#include "Elevation.hpp"
#include "EnumTraits.hpp"
#include "Facing.hpp"
#include "Hitbox.hpp"
#include "IRenderer.hpp"
#include "Logger.hpp"
#include "MotionSystem.hpp"
#include "Motor.hpp"
#include "PlayerInputState.hpp"
#include "PlayerModes.hpp"
#include "PlayerMovementState.hpp"
#include "PlayerMovementSystem.hpp"
#include "PlayerSprite.hpp"
#include "Speed.hpp"
#include "Texture.hpp"
#include "TextureStore.hpp"
#include "TileMath.hpp"
#include "Transform.hpp"
#include "WorldServices.hpp"

#include <string>
#include <vector>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Player";

// Shared empty texture returned by the sheet accessors when no store is bound.
// Static so callers can safely bind a reference even when textures are absent.
const Texture& EmptyPlayerTexture()
{
    static const Texture empty;
    return empty;
}

// The TextureStore published in globals, or nullptr when no WorldServices is
// registered (e.g. a bare test world with no renderer/services wired up).
TextureStore* TexturesOf(const ecs::registry& world)
{
    const WorldServices* svc = world.globals().find<WorldServices>();
    return (svc != nullptr) ? svc->textures : nullptr;
}

// The AssetRegistry published in globals, or nullptr (see TexturesOf).
AssetRegistry* AssetsOf(const ecs::registry& world)
{
    const WorldServices* svc = world.globals().find<WorldServices>();
    return (svc != nullptr) ? svc->assets : nullptr;
}
}  // namespace

namespace PlayerSystem
{
bool SwitchCharacter(ecs::registry& world, ecs::entity player, CharacterType type)
{
    const auto typeName = EnumTraits<CharacterType>::ToString(type);

    // Both services are mandatory: the AssetRegistry resolves the sprite paths and
    // the TextureStore loads them. Bail before touching any component so a failed
    // switch leaves the player's current appearance intact.
    AssetRegistry* assets = AssetsOf(world);
    TextureStore* textures = TexturesOf(world);
    if (assets == nullptr)
    {
        Logger::Error(LOG_SUBSYSTEM, "SwitchCharacter called with no AssetRegistry in globals");
        return false;
    }
    if (textures == nullptr)
    {
        Logger::Error(LOG_SUBSYSTEM, "SwitchCharacter called with no TextureStore in globals");
        return false;
    }

    auto getAssetPath = [&](const std::string& spriteType) -> std::string
    {
        std::string path = assets->ResolveCharacterAsset(type, spriteType);
        if (path.empty())
        {
            Logger::ErrorF(LOG_SUBSYSTEM, "No asset registered for {} {}", typeName, spriteType);
        }
        return path;
    };

    // Acquire into temporaries so committed handles only change on success.
    auto acquire = [&](const std::string& path) -> TextureHandle
    { return path.empty() ? TextureHandle{} : textures->Acquire(path); };

    TextureHandle newWalking = acquire(getAssetPath("Walking"));
    TextureHandle newRunning = acquire(getAssetPath("Running"));
    TextureHandle newBicycle = acquire(getAssetPath("Bicycle"));

    // Walk + run are required; a missing either sheet aborts the switch.
    if (!textures->IsValid(newWalking) || !textures->IsValid(newRunning))
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Failed to load character sprites for {}", typeName);
        return false;
    }

    // Bicycle is optional: warn and keep the previous bicycle sheet if absent.
    if (!textures->IsValid(newBicycle))
    {
        Logger::WarnF(LOG_SUBSYSTEM, "Bicycle sprite not found for {}", typeName);
    }

    // Past all validation - commit the new handles onto the components.
    auto& sprite = world.get<PlayerSprite>(player);
    auto& appearance = world.get<Appearance>(player);
    sprite.walk = newWalking;
    sprite.run = newRunning;
    if (textures->IsValid(newBicycle))
    {
        sprite.bicycle = newBicycle;
    }
    appearance.characterType = type;
    // Walking sheet swapped; re-sample the accent color from the new pixels.
    appearance.accentColor =
        textures->SampleAccent(sprite.walk, ambience::DIALOGUE_ACCENT_FALLBACK);

    Logger::InfoF(LOG_SUBSYSTEM, "Switched to {}", typeName);
    return true;
}

bool CopyAppearanceFrom(ecs::registry& world, ecs::entity player, const std::string& spritePath)
{
    // Disguise the player as an NPC by swapping in that NPC's walking sheet.
    TextureStore* textures = TexturesOf(world);
    if (textures == nullptr)
    {
        Logger::Error(LOG_SUBSYSTEM, "CopyAppearanceFrom called with no TextureStore in globals");
        return false;
    }

    // NPC sheets have only a walking sprite; running/bicycle auto-restore the
    // original, so only the walking sheet needs replacing.
    const TextureHandle disguise = textures->Acquire(spritePath);
    if (!textures->IsValid(disguise))
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Failed to copy appearance from: {}", spritePath);
        return false;
    }

    auto& sprite = world.get<PlayerSprite>(player);
    auto& appearance = world.get<Appearance>(player);
    sprite.walk = disguise;
    appearance.usingCopiedAppearance = true;
    appearance.accentColor =
        textures->SampleAccent(sprite.walk, ambience::DIALOGUE_ACCENT_FALLBACK);
    Logger::InfoF(LOG_SUBSYSTEM, "Copied appearance from: {}", spritePath);
    return true;
}

void RestoreOriginalAppearance(ecs::registry& world, ecs::entity player)
{
    auto& appearance = world.get<Appearance>(player);
    if (!appearance.usingCopiedAppearance)
    {
        return;
    }
    // Reload original character sprites - only clear the flag on success.
    if (SwitchCharacter(world, player, appearance.characterType))
    {
        world.get<Appearance>(player).usingCopiedAppearance = false;
        Logger::Info(LOG_SUBSYSTEM, "Restored original appearance");
    }
    else
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to restore original appearance");
    }
}

void UploadTextures(const ecs::registry& world, ecs::entity player, IRenderer& renderer)
{
    // Re-push the player's three sheets to the GPU; needed after a character switch
    // or a renderer/backend swap (which drops previously uploaded textures).
    TextureStore* textures = TexturesOf(world);
    if (textures == nullptr)
    {
        return;
    }
    const auto& sprite = world.get<PlayerSprite>(player);
    renderer.UploadTexture(textures->Get(sprite.walk));
    renderer.UploadTexture(textures->Get(sprite.run));
    renderer.UploadTexture(textures->Get(sprite.bicycle));
}

void SetAtlasBinding(ecs::registry& world,
                     ecs::entity player,
                     const Texture* atlasTex,
                     glm::vec2 walkOffset,
                     glm::vec2 runOffset,
                     glm::vec2 bicycleOffset)
{
    // Point the player's sprite at a shared atlas plus per-mode pixel offsets; the
    // render path folds these offsets into the sampled UVs. A null atlas reverts to
    // the per-player walk/run/bicycle sheets.
    auto& sprite = world.get<PlayerSprite>(player);
    sprite.atlas = atlasTex;
    sprite.atlasWalkOffset = walkOffset;
    sprite.atlasRunOffset = runOffset;
    sprite.atlasBicycleOffset = bicycleOffset;
}

// Sheet accessors: resolve a handle through the globals TextureStore, falling back
// to the shared empty texture when no store is bound (e.g. headless test worlds).
const Texture& GetSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite)
{
    TextureStore* textures = TexturesOf(world);
    return (textures != nullptr) ? textures->Get(sprite.walk) : EmptyPlayerTexture();
}

const Texture& GetRunningSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite)
{
    TextureStore* textures = TexturesOf(world);
    return (textures != nullptr) ? textures->Get(sprite.run) : EmptyPlayerTexture();
}

const Texture& GetBicycleSpriteSheet(const ecs::registry& world, const PlayerSprite& sprite)
{
    TextureStore* textures = TexturesOf(world);
    return (textures != nullptr) ? textures->Get(sprite.bicycle) : EmptyPlayerTexture();
}

void Update(ecs::registry& world, ecs::entity player, float deltaTime)
{
    // Per-frame cosmetic advance only: velocity-driven walk cadence + smooth
    // elevation lerp. Positional movement is applied separately in Move().
    auto& anim = world.get<AnimationState>(player);
    auto& elev = world.get<Elevation>(player);
    PlayerMovementSystem::UpdateAnimation(anim,
                                          world.get<PlayerModes>(player),
                                          world.get<Motor>(player),
                                          deltaTime,
                                          CharacterConstants::ANIM_FRAME_DURATION);
    CharacterKinematics::UpdateElevation(elev, deltaTime);
}

void Move(ecs::registry& world,
          ecs::entity player,
          glm::vec2 direction,
          float deltaTime,
          const Tilemap* tilemap,
          const std::vector<glm::vec2>* npcPositions)
{
    // Fan the player's components out to the stateless movement step. Collision
    // response and per-frame hysteresis (slide / lane-snap / stuck recovery) are
    // threaded through the PlayerMovementState component, not held here.
    PlayerMovementSystem::Step(world.get<Transform>(player),
                               world.get<Motor>(player),
                               world.get<Facing>(player),
                               world.get<AnimationState>(player),
                               world.get<Elevation>(player),
                               world.get<PlayerModes>(player),
                               world.get<PlayerInputState>(player),
                               world.get<PlayerMovementState>(player),
                               world.get<Speed>(player),
                               world.get<Hitbox>(player),
                               direction,
                               deltaTime,
                               tilemap,
                               npcPositions);
}

void Stop(ecs::registry& world, ecs::entity player)
{
    // Halt motion and drop back to the idle animation frame.
    PlayerMovementSystem::Stop(world.get<AnimationState>(player),
                               world.get<PlayerInputState>(player),
                               world.get<PlayerModes>(player),
                               world.get<Motor>(player));
}

void SetTilePosition(ecs::registry& world, ecs::entity player, int tileX, int tileY)
{
    // Snap the feet anchor to the tile's bottom-center, then reset the motor so no
    // residual velocity or stop-target carries across the teleport.
    // NOTE: tile size is hardcoded 16px here; everywhere else it comes from the
    // project manifest (tileWidth/tileHeight), so non-16px projects would mis-snap.
    world.get<Transform>(player).position = TileMath::TileFeetCenter(tileX, tileY, 16.0f);
    MotionSystem::Reset(world.get<Motor>(player));
}

void SetPositionRaw(ecs::registry& world, ecs::entity player, glm::vec2 pos)
{
    // Exact-position variant (no tile snapping); used by dialogue alignment to place
    // the player precisely. Also resets the motor, like SetTilePosition.
    world.get<Transform>(player).position = pos;
    MotionSystem::Reset(world.get<Motor>(player));
}
}  // namespace PlayerSystem
