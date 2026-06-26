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
 * Demoted from the former @c PlayerCharacter::s_CharacterAssets and
 * @c NonPlayerCharacter::s_NpcAssets class statics. Those mutable globals were
 * invisible to any owner, survived a notional world reset, and broke test
 * isolation. Here the tables are owned state: @ref Game holds one instance and
 * the spawn/switch paths resolve through it (entities hold a non-owning pointer,
 * mirroring @ref TextureStore / @ref DialogueStore). At ECS integration this
 * becomes a @c globals() singleton the systems read, with no API change.
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
    void SetNpcAsset(const std::string& type, const std::string& path);

    /// @brief Resolve an NPC sprite path, or the legacy assets/non-player fallback.
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
