#pragma once

#include "CharacterType.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class AssetRegistry
 * @brief World-reachable registry of character + NPC sprite asset paths.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Two independent lookup tables of sprite-sheet file paths: player characters keyed
 * by (@c CharacterType, sprite type), NPCs keyed by type id. Paths only - the pixels
 * belong to @ref TextureStore. Both tables are filled once at startup from the
 * project manifest (@c playerCharacters / @c npcSprites).
 *
 * @ref Game holds one instance by value and publishes a non-owning pointer to it as
 * @c WorldServices::assets in the registry's @c globals(), mirroring
 * @ref TextureStore / @ref DialogueStore. No entity holds a pointer - the
 * spawn/switch paths (@c EntityStore::SpawnNpc, @c PlayerSystem) read it out of the
 * world.
 */
class AssetRegistry
{
public:
    /// @brief Register a player character sprite path, keyed by (type, spriteType).
    void SetCharacterAsset(CharacterType type,
                           const std::string& spriteType,
                           const std::string& path);

    /// @brief Resolve a player character sprite path, or "" if unregistered.
    [[nodiscard]] std::string ResolveCharacterAsset(CharacterType type,
                                                    const std::string& spriteType) const;

    /// @brief Register an NPC sprite path for a type id (filename without extension).
    /// An empty @p type is ignored silently; a known type id has its path overwritten.
    void SetNpcAsset(const std::string& type, const std::string& path);

    /**
     * @brief Resolve an NPC sprite path, or the assets/non-player fallback.
     *
     * The two results have different resolution bases. A registered path comes back exactly
     * as stored, already resolved through @ref ProjectManifest by whoever registered it. An
     * unregistered id yields the literal `assets/non-player/<type>.png`, which is relative to
     * the process working directory and ignores the manifest base directory, so it misses
     * whenever the game runs from anywhere but the project root.
     */
    [[nodiscard]] std::string ResolveNpcAsset(const std::string& type) const;

    /// @brief Registered NPC type ids, unspecified order (npc.spawn autocomplete).
    [[nodiscard]] std::vector<std::string> AvailableNpcTypes() const;

private:
    /// @brief Key for the player character asset table.
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

    std::map<CharacterAssetKey, std::string> m_CharacterAssets;
    std::unordered_map<std::string, std::string> m_NpcAssets;
};
