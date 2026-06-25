#include "EntityStore.hpp"

#include "AmbienceConfig.hpp"
#include "AnimationState.hpp"
#include "Appearance.hpp"
#include "AssetRegistry.hpp"
#include "CharacterConstants.hpp"
#include "Dialogue.hpp"
#include "DialogueStore.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "Hitbox.hpp"
#include "Identity.hpp"
#include "IRenderer.hpp"
#include "Logger.hpp"
#include "Motor.hpp"
#include "NpcIdle.hpp"
#include "NpcRecord.hpp"
#include "NpcSprite.hpp"
#include "NpcTag.hpp"
#include "Patrol.hpp"
#include "PatrolRoute.hpp"
#include "PlayerInputState.hpp"
#include "PlayerModes.hpp"
#include "PlayerMovementState.hpp"
#include "PlayerSprite.hpp"
#include "PlayerTag.hpp"
#include "Speed.hpp"
#include "TextureStore.hpp"
#include "Transform.hpp"
#include "WorldServices.hpp"

#include <string>
#include <utility>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "NPC";

/// Default greeting seeded into a spawned NPC's simple dialogue (matches the
/// former NonPlayerCharacter constructor).
constexpr const char* DEFAULT_NPC_TEXT = "Hello! How are you today?";

/// Monotonic source of per-session NPC instance ids (was a file-local static in
/// NonPlayerCharacter.cpp). Gives each NPC a stable identity distinct from its
/// entity handle so dialogue/editor/console references survive despawn/undo.
/// Single-threaded (NPCs are only created on the game thread). Not serialized.
std::uint64_t NextNpcInstanceId()
{
    static std::uint64_t s_Next = 1;
    return s_Next++;
}
}  // namespace

namespace EntityStore
{
void SetNpcTile(Transform& xf,
                Patrol& patrol,
                PatrolRoute& route,
                int tileX,
                int tileY,
                int tileSize,
                bool preserveRoute)
{
    patrol.tileX = tileX;
    patrol.tileY = tileY;

    // Position at bottom-center of tile.
    xf.position.x = tileX * tileSize + tileSize * 0.5f;
    xf.position.y = tileY * tileSize + static_cast<float>(tileSize);

    patrol.targetTileX = tileX;
    patrol.targetTileY = tileY;

    if (!preserveRoute)
    {
        route.Reset();
    }
}

ecs::entity SpawnNpc(ecs::registry& world, const NpcRecord& record, IRenderer* uploadVia)
{
    // Services come from globals (null-tolerant, mirroring the old per-entity
    // pointer fallbacks; tests may run without WorldServices published).
    const WorldServices* svc = world.globals().find<WorldServices>();
    TextureStore* textures = (svc != nullptr) ? svc->textures : nullptr;
    DialogueStore* dialogue = (svc != nullptr) ? svc->dialogue : nullptr;
    AssetRegistry* assets = (svc != nullptr) ? svc->assets : nullptr;

    const std::string spritePath = (assets != nullptr)
                                       ? assets->ResolveNpcAsset(record.type)
                                       : ("assets/non-player/" + record.type + ".png");

    TextureHandle sheet{};
    glm::vec3 accent = ambience::DIALOGUE_ACCENT_FALLBACK;
    if (textures != nullptr)
    {
        sheet = textures->Acquire(spritePath);
        if (!textures->IsValid(sheet))
        {
            Logger::ErrorF(LOG_SUBSYSTEM, "SpawnNpc: failed to load NPC sprite: {}", spritePath);
            return ecs::entity{};
        }
        accent = textures->SampleAccent(sheet, ambience::DIALOGUE_ACCENT_FALLBACK);
    }

    DialogueHandle treeHandle{};
    if (dialogue != nullptr && record.hasTree)
    {
        treeHandle = dialogue->Add(record.tree);
    }

    Transform xf{};
    Patrol patrol{};
    PatrolRoute route{};
    SetNpcTile(xf, patrol, route, record.tileX, record.tileY, record.tileSize, /*preserve=*/false);

    Facing facing{};
    facing.dir = record.facing;

    Dialogue dialogueComp;
    dialogueComp.type = record.type;
    dialogueComp.name = record.name;
    dialogueComp.text = record.text.empty() ? DEFAULT_NPC_TEXT : record.text;
    dialogueComp.tree = treeHandle;

    NpcSprite sprite;
    sprite.sheet = sheet;
    sprite.atlas = nullptr;
    sprite.atlasOffset = glm::vec2(0.0f);
    sprite.accentColor = accent;

    Speed speed;
    speed.value = CharacterConstants::NPC_BASE_SPEED;

    Identity identity;
    identity.instanceId = (record.instanceId != 0) ? record.instanceId : NextNpcInstanceId();

    const ecs::entity e = world.create(std::move(xf),
                                       Elevation{},
                                       facing,
                                       AnimationState{},
                                       speed,
                                       identity,
                                       std::move(sprite),
                                       std::move(dialogueComp),
                                       NpcIdle{},
                                       std::move(patrol),
                                       std::move(route));
    world.add<NpcTag>(e);

    if (uploadVia != nullptr && textures != nullptr)
    {
        uploadVia->UploadTexture(textures->Get(sheet));
    }
    return e;
}

NpcRecord SnapshotNpc(const ecs::registry& world, ecs::entity e)
{
    NpcRecord rec;
    const Dialogue& dialogue = world.get<Dialogue>(e);
    const Patrol& patrol = world.get<Patrol>(e);
    const Identity& identity = world.get<Identity>(e);
    const Facing& facing = world.get<Facing>(e);

    rec.type = dialogue.type;
    rec.name = dialogue.name;
    rec.text = dialogue.text;
    rec.tileX = patrol.tileX;
    rec.tileY = patrol.tileY;
    rec.tileSize = 16;  // project tile size; tile -> feet round-trips identically.
    rec.instanceId = identity.instanceId;  // preserve identity across respawn (undo/redo).
    rec.facing = facing.dir;

    const WorldServices* svc = world.globals().find<WorldServices>();
    if (svc != nullptr && svc->dialogue != nullptr && svc->dialogue->HasTree(dialogue.tree))
    {
        rec.tree = svc->dialogue->Get(dialogue.tree);
        rec.hasTree = true;
    }
    return rec;
}

ecs::entity SpawnPlayer(ecs::registry& world, glm::vec2 spawnPos)
{
    Transform xf{};
    xf.position = spawnPos;
    Speed speed{};
    speed.value = CharacterConstants::PLAYER_BASE_SPEED;
    Appearance appearance{};
    appearance.accentColor = ambience::DIALOGUE_ACCENT_FALLBACK;
    PlayerMovementState movement{};
    movement.lastSafeTileCenter = spawnPos;

    // No Identity component: the player is referenced solely by its m_PlayerEntity
    // handle (never by a despawn-surviving instanceId the way NPCs are), so the
    // stable-id indirection that Identity provides would be dead weight here.
    const ecs::entity e = world.create(std::move(xf),
                                       Elevation{},
                                       Facing{},
                                       AnimationState{},
                                       speed,
                                       std::move(appearance),
                                       PlayerModes{},
                                       PlayerInputState{},
                                       std::move(movement),
                                       Motor{},
                                       PlayerSprite{},
                                       Hitbox{});
    world.add<PlayerTag>(e);
    return e;
}

void Remove(ecs::registry& world, ecs::entity e)
{
    if (world.alive(e))
    {
        world.destroy(e);
    }
}

void Clear(ecs::registry& world)
{
    // Collect first, then destroy: mutating a pool while iterating it is a fault.
    const std::vector<ecs::entity> doomed = Entities(world);
    for (const ecs::entity e : doomed)
    {
        world.destroy(e);
    }
}

std::size_t Count(ecs::registry& world)
{
    return world.view<NpcTag>().count();
}

std::vector<ecs::entity> Entities(ecs::registry& world)
{
    std::vector<ecs::entity> out;
    out.reserve(world.view<NpcTag>().count());
    world.each<NpcTag>([&out](ecs::entity e) { out.push_back(e); });
    return out;
}

ecs::entity FindById(ecs::registry& world, std::uint64_t instanceId)
{
    if (instanceId == 0)
    {
        return ecs::entity{};
    }
    ecs::entity found{};
    world.each<const Identity, const NpcTag>(
        [&](ecs::entity e, const Identity& id)
        {
            if (id.instanceId == instanceId)
            {
                found = e;
                return false;  // instanceId is unique: stop at the first match.
            }
            return true;
        });
    return found;
}

ecs::entity FindById(const ecs::registry& world, std::uint64_t instanceId)
{
    if (instanceId == 0)
    {
        return ecs::entity{};
    }
    ecs::entity found{};
    world.each<const Identity, const NpcTag>(
        [&](ecs::entity e, const Identity& id)
        {
            if (id.instanceId == instanceId)
            {
                found = e;
                return false;  // instanceId is unique: stop at the first match.
            }
            return true;
        });
    return found;
}
}  // namespace EntityStore

void BuildNpcFeet(ecs::registry& world, std::vector<glm::vec2>& out)
{
    out.clear();
    out.reserve(world.view<NpcTag>().count());
    world.each<const Transform, const NpcTag>([&out](const Transform& xf)
                                              { out.push_back(xf.position); });
}
