#pragma once

#include "CharacterDirection.hpp"

#include <ecs.hpp>

#include <glm/glm.hpp>

#include <string>

class IRenderer;
class Texture;
struct Transform;
struct Elevation;
struct Facing;
struct AnimationState;
struct NpcSprite;

/**
 * @file NpcRender.hpp
 * @brief Free-function NPC sprite rendering over granular components.
 * @ingroup Rendering
 *
 * The render half-draw mechanics carved out of the former
 * @c NonPlayerCharacter::RenderBottomHalf / RenderTopHalf / DrawHalf /
 * ResolveRenderSheet / GetSpriteCoords methods. Operate purely on the granular
 * components (read by reference) plus the @c TextureStore resolved from
 * @c world.globals().get<WorldServices>(), so the Y-sort render pass can draw an
 * NPC from its entity handle without any NPC object. Renderer-touching but only
 * via @ref IRenderer, so it links into the test library.
 */
namespace NpcRender
{
/// @brief Sprite-sheet UV top-left (pixels) for a frame + facing (NPC row map).
glm::vec2 SpriteCoords(int frame, CharacterDirection dir);

/// @brief Select the draw sheet (shared tile atlas when bound, else the per-NPC
/// sheet from the TextureStore in globals) and, when atlas-bound, fold the atlas
/// offset into @p spriteCoords.
const Texture& ResolveRenderSheet(const ecs::registry& world,
                                  const NpcSprite& sprite,
                                  glm::vec2& spriteCoords);

/// @brief Draw the top or bottom half of an NPC sprite for the Y-sort pass.
/// Reads the five render components; resolves sheet + UVs; defers projection +
/// the perspective-suspended draw to @ref CharacterRender.
void DrawHalf(const ecs::registry& world,
              IRenderer& renderer,
              glm::vec2 cameraPos,
              bool topHalf,
              const Transform& xf,
              const Elevation& elev,
              const Facing& facing,
              const AnimationState& anim,
              const NpcSprite& sprite);
}  // namespace NpcRender

/**
 * @brief Pure string utility: the NPC type identifier from a sprite path.
 * @ingroup Entities
 *
 * Filename without the @c .png extension. The free-function form of the former
 * @c NonPlayerCharacter::TypeFromSpritePath (no asset-table state; resolution
 * lives in @ref AssetRegistry).
 */
namespace NpcType
{
std::string FromSpritePath(const std::string& path);
}  // namespace NpcType
