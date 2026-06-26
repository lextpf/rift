#include "AssetRegistry.hpp"

void AssetRegistry::SetCharacterAsset(CharacterType type,
                                      const std::string& spriteType,
                                      const std::string& path)
{
    m_CharacterAssets[CharacterAssetKey{type, spriteType}] = path;
}

std::string AssetRegistry::ResolveCharacterAsset(CharacterType type,
                                                 const std::string& spriteType) const
{
    auto it = m_CharacterAssets.find(CharacterAssetKey{type, spriteType});
    return it != m_CharacterAssets.end() ? it->second : std::string();
}

void AssetRegistry::SetNpcAsset(const std::string& type, const std::string& path)
{
    if (!type.empty())
    {
        m_NpcAssets[type] = path;
    }
}

std::string AssetRegistry::ResolveNpcAsset(const std::string& type) const
{
    auto it = m_NpcAssets.find(type);
    if (it != m_NpcAssets.end())
    {
        return it->second;
    }
    return "assets/non-player/" + type + ".png";
}

std::vector<std::string> AssetRegistry::AvailableNpcTypes() const
{
    std::vector<std::string> types;
    types.reserve(m_NpcAssets.size());
    for (const auto& [type, path] : m_NpcAssets)
    {
        types.push_back(type);
    }
    return types;
}
