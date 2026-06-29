#pragma once

#include <random>

class TextureStore;
class DialogueStore;
class AssetRegistry;
class GameStateManager;

/**
 * @struct WorldServices
 * @brief Non-owning bundle of the world's shared services, stored in the ECS
 *        @c globals() singleton.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * The seam that replaces per-entity service back-pointers (@c m_Store /
 * @c m_DialogueStore / @c m_Assets) during the ECS migration. Game owns the
 * three services by value and publishes pointers to them here, into
 * `registry.globals()`, so the component-only systems (and the spawn path) reach
 * them through the world instead of via the entity classes. Plain aggregate of
 * non-owning pointers; the services outlive the registry (Game owns both).
 */
struct WorldServices
{
    TextureStore* textures = nullptr;
    DialogueStore* dialogue = nullptr;
    AssetRegistry* assets = nullptr;
    /**
     * @brief Shared NPC-AI random source. Game owns the engine by value (@c m_NpcRng)
     * and publishes this pointer so @ref NpcAiSystem (and non-Game callers like the
     * editor's navigation recalc) draw idle/look/pause randomness from one world-scoped
     * stream via @c registry.globals() instead of a hidden file-local static.
     */
    std::mt19937* npcRng = nullptr;
    /**
     * @brief Save-game flag/quest store. Published here for uniformity so a future
     * component-only condition/consequence system can read or mutate flags through the
     * world rather than a Game pointer. (DialogueManager still uses its injected pointer.)
     */
    GameStateManager* gameState = nullptr;
};
