#include "NpcRender.hpp"

#include "AnimationState.hpp"
#include "CharacterConstants.hpp"
#include "CharacterRender.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "NpcSprite.hpp"
#include "Texture.hpp"
#include "TextureStore.hpp"
#include "Transform.hpp"
#include "WorldServices.hpp"

#include <algorithm>
#include <cctype>

namespace
{
/// Shared empty texture for the sheet accessor when no store is bound.
const Texture& EmptyNpcTexture()
{
    static const Texture empty;
    return empty;
}

/// The NPC's per-sheet texture, resolved through the TextureStore in globals.
const Texture& NpcSheet(const ecs::registry& world, const NpcSprite& sprite)
{
    const WorldServices* svc = world.globals().find<WorldServices>();
    return (svc != nullptr && svc->textures != nullptr) ? svc->textures->Get(sprite.sheet)
                                                        : EmptyNpcTexture();
}
}  // namespace

glm::vec2 NpcRender::SpriteCoords(int frame, CharacterDirection dir)
{
    int spriteX = (frame % CharacterConstants::WALK_FRAME_COUNT) * CharacterConstants::SPRITE_WIDTH;
    int spriteY = 0;

    switch (dir)
    {
        case CharacterDirection::DOWN:
            spriteY = 2 * CharacterConstants::SPRITE_HEIGHT;
            break;
        case CharacterDirection::UP:
            spriteY = 3 * CharacterConstants::SPRITE_HEIGHT;
            break;
        case CharacterDirection::LEFT:
            spriteY = 1 * CharacterConstants::SPRITE_HEIGHT;
            break;
        case CharacterDirection::RIGHT:
            spriteY = 0 * CharacterConstants::SPRITE_HEIGHT;
            break;
        default:
            spriteY = 2 * CharacterConstants::SPRITE_HEIGHT;
            break;
    }

    return glm::vec2(static_cast<float>(spriteX), static_cast<float>(spriteY));
}

const Texture& NpcRender::ResolveRenderSheet(const ecs::registry& world,
                                             const NpcSprite& sprite,
                                             glm::vec2& spriteCoords)
{
    // Atlas binding: draw out of the shared tile atlas with the baked offset so all
    // NPCs share one texture and batch with the tiles; fall back to the per-NPC
    // sheet otherwise. The offset is in GL-row space (matching the standalone
    // sheet's stbi-flipped layout), so the caller's flipY stays false.
    if (sprite.atlas != nullptr)
    {
        spriteCoords += sprite.atlasOffset;
        return *sprite.atlas;
    }
    return NpcSheet(world, sprite);
}

void NpcRender::DrawHalf(const ecs::registry& world,
                         IRenderer& renderer,
                         glm::vec2 cameraPos,
                         bool topHalf,
                         const Transform& xf,
                         const Elevation& elev,
                         const Facing& facing,
                         const AnimationState& anim,
                         const NpcSprite& sprite)
{
    const glm::vec2 spriteSize(static_cast<float>(CharacterConstants::SPRITE_WIDTH),
                               static_cast<float>(CharacterConstants::SPRITE_HEIGHT));
    glm::vec2 spriteCoords = SpriteCoords(anim.currentFrame, facing.dir);
    const Texture& sheet = ResolveRenderSheet(world, sprite, spriteCoords);
    glm::vec2 renderPos = CharacterRender::ComputeRenderPos(
        renderer, xf.position, cameraPos, elev.offset, spriteSize);
    CharacterRender::DrawPart(
        renderer,
        sheet,
        renderPos,
        spriteCoords,
        spriteSize,
        topHalf ? CharacterRender::Part::TopHalf : CharacterRender::Part::BottomHalf,
        /*suspendPerspective=*/true);
}

std::string NpcType::FromSpritePath(const std::string& path)
{
    size_t lastSlash = path.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

    if (filename.size() > 4)
    {
        std::string ext = filename.substr(filename.size() - 4);
        std::transform(ext.begin(),
                       ext.end(),
                       ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".png")
        {
            return filename.substr(0, filename.size() - 4);
        }
    }

    return filename;
}
