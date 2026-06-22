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
    /// @brief The world's shared NPC-AI random source. Game owns the engine by
    /// value (@c m_NpcRng) and publishes a pointer here so @ref NpcAiSystem can
    /// draw idle/look-around/pause randomness from one world-scoped stream
    /// instead of a hidden file-local static. Systems take the engine by
    /// reference; this pointer is how non-Game callers (e.g. the navigation
    /// recalc on editor strokes) reach it via @c registry.globals().
    std::mt19937* npcRng = nullptr;
    /// @brief The save-game flag/quest store. Published here for uniformity with
    /// the other services so a future component-only condition/consequence system
    /// can read or mutate flags through the world rather than via a Game pointer.
    /// (DialogueManager still reaches it via its injected pointer today.)
    GameStateManager* gameState = nullptr;
};
