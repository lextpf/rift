#pragma once

#include <random>

class TextureStore;
class DialogueStore;
class AssetRegistry;
class GameStateManager;

/**
 * @struct WorldServices
 * @brief Non-owning bundle of the world's shared services, stored in the ECS @c globals()
 *        singleton.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * This is how stateless systems reach shared state. Game owns all five services below by value and
 * publishes pointers to them here, into `registry.globals()`. A system holding nothing but
 * component references can then resolve a texture or a dialogue tree, without duplicating the
 * services onto every entity. The services outlive the registry, since Game owns both.
 *
 * Every pointer may be null. Tests routinely publish a partial bundle or none at all, so readers
 * must null-check rather than assume a fully wired world.
 */
struct WorldServices
{
    /// GPU sprite sheets keyed by TextureHandle. NPC spawn resolves its sheet and accent color
    /// through this; a null pointer makes SpawnNpc skip texture work entirely.
    TextureStore* textures = nullptr;
    /// Owner of the branching DialogueTree graphs keyed by DialogueHandle. A null pointer leaves a
    /// spawned NPC with only its simple fallback line.
    DialogueStore* dialogue = nullptr;
    /// Player and NPC sprite asset path tables. A null pointer falls back to the
    /// `assets/non-player/&lt;type&gt;.png` path convention.
    AssetRegistry* assets = nullptr;
    /**
     * @brief Shared NPC-AI random source.
     *
     * Game owns the engine by value as @c m_NpcRng and publishes this pointer, so @ref NpcAiSystem
     * and non-Game callers such as the editor's navigation recalc all draw idle, look and pause
     * randomness from one world-scoped stream instead of a file-local static.
     */
    std::mt19937* npcRng = nullptr;
    /**
     * @brief Session flag and quest store.
     *
     * Published here so a component-only condition and consequence system could read or mutate
     * flags through the world rather than a Game pointer. DialogueManager uses its injected
     * pointer instead.
     */
    GameStateManager* gameState = nullptr;
};
