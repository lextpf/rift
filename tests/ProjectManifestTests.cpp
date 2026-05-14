#include <gtest/gtest.h>

#include "../src/EnumTraits.hpp"
#include "../src/PlayerCharacter.hpp"
#include "../src/ProjectManifest.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>

namespace
{
class TempProject
{
public:
    TempProject()
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("rift_project_manifest_test_" + std::to_string(stamp));
        std::filesystem::create_directories(root);
    }

    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    std::filesystem::path root;

    void Touch(const std::string& relativePath) const
    {
        std::filesystem::path path = root / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << "fixture";
    }

    std::filesystem::path WriteManifest(const std::string& json) const
    {
        std::filesystem::path path = root / ProjectManifest::DEFAULT_FILENAME;
        std::ofstream file(path);
        file << json;
        return path;
    }
};

class CurrentPathGuard
{
public:
    explicit CurrentPathGuard(const std::filesystem::path& newPath)
        : m_OldPath(std::filesystem::current_path())
    {
        std::filesystem::current_path(newPath);
    }

    ~CurrentPathGuard() { std::filesystem::current_path(m_OldPath); }

private:
    std::filesystem::path m_OldPath;
};

void CreateValidAssetFixture(const TempProject& project)
{
    project.Touch("assets/tiles.png");
    project.Touch("assets/player/walk.png");
    project.Touch("assets/player/run.png");
    project.Touch("assets/player/bike.png");
    project.Touch("assets/non-player/npc.png");
    project.Touch("assets/fonts/ui.ttf");
    project.Touch("rift.save.json");
}

std::string ValidManifestJson()
{
    return R"json({
        "formatVersion": 1,
        "startupRenderer": "OpenGL",
        "defaultMap": "rift.save.json",
        "tileWidth": 16,
        "tileHeight": 16,
        "defaultMapSize": { "width": 32, "height": 24 },
        "tilesets": [ "assets/tiles.png" ],
        "npcSprites": [ "assets/non-player/npc.png" ],
        "fonts": [ "assets/fonts/ui.ttf" ],
        "playerCharacters": {
            "BW1_MALE": {
                "Walking": "assets/player/walk.png",
                "Running": "assets/player/run.png",
                "Bicycle": "assets/player/bike.png"
            }
        }
    })json";
}

bool HasDiagnosticFor(const ManifestValidationResult& result, std::string fieldPath)
{
    return std::ranges::any_of(result.diagnostics,
                               [&](const ManifestDiagnostic& diagnostic)
                               { return diagnostic.fieldPath == fieldPath; });
}
}  // namespace

TEST(ProjectManifestTests, ValidManifestParsesAndResolvesRelativePaths)
{
    TempProject project;
    CreateValidAssetFixture(project);
    std::filesystem::path path = project.WriteManifest(ValidManifestJson());

    ManifestValidationResult result;
    std::optional<ProjectManifest> manifest = ProjectManifest::LoadFromFile(path, result);

    ASSERT_TRUE(manifest.has_value());
    EXPECT_FALSE(result.HasErrors());
    EXPECT_TRUE(manifest->loadedFromFile);
    EXPECT_EQ(manifest->startupRenderer, "OpenGL");
    EXPECT_EQ(manifest->defaultMapWidth, 32);
    EXPECT_EQ(manifest->defaultMapHeight, 24);
    EXPECT_EQ(manifest->ResolvePath("assets/tiles.png"), project.root / "assets/tiles.png");
}

TEST(ProjectManifestTests, MalformedJsonFailsWithError)
{
    TempProject project;
    std::filesystem::path path = project.WriteManifest("{ invalid json");

    ManifestValidationResult result;
    std::optional<ProjectManifest> manifest = ProjectManifest::LoadFromFile(path, result);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_TRUE(result.HasErrors());
}

TEST(ProjectManifestTests, MissingRequiredTilesetIsAnError)
{
    TempProject project;
    CreateValidAssetFixture(project);
    std::filesystem::path path = project.WriteManifest(R"json({
        "formatVersion": 1,
        "startupRenderer": "OpenGL",
        "defaultMap": "rift.save.json",
        "tileWidth": 16,
        "tileHeight": 16,
        "defaultMapSize": { "width": 32, "height": 24 },
        "tilesets": [ "assets/missing.png" ],
        "npcSprites": [ "assets/non-player/npc.png" ],
        "fonts": [ "assets/fonts/ui.ttf" ],
        "playerCharacters": {
            "BW1_MALE": {
                "Walking": "assets/player/walk.png",
                "Running": "assets/player/run.png"
            }
        }
    })json");

    ManifestValidationResult result;
    std::optional<ProjectManifest> manifest = ProjectManifest::LoadFromFile(path, result);

    ASSERT_TRUE(manifest.has_value());
    EXPECT_TRUE(result.HasErrors());
    EXPECT_TRUE(HasDiagnosticFor(result, "tilesets[0]"));
}

TEST(ProjectManifestTests, MissingDefaultMapWarnsButDoesNotError)
{
    TempProject project;
    CreateValidAssetFixture(project);
    std::filesystem::remove(project.root / "rift.save.json");
    std::filesystem::path path = project.WriteManifest(ValidManifestJson());

    ManifestValidationResult result;
    std::optional<ProjectManifest> manifest = ProjectManifest::LoadFromFile(path, result);

    ASSERT_TRUE(manifest.has_value());
    EXPECT_FALSE(result.HasErrors());
    EXPECT_TRUE(result.HasWarnings());
    EXPECT_TRUE(HasDiagnosticFor(result, "defaultMap"));
}

TEST(ProjectManifestTests, UnknownCharacterNameIsAnError)
{
    TempProject project;
    CreateValidAssetFixture(project);
    std::filesystem::path path = project.WriteManifest(R"json({
        "formatVersion": 1,
        "startupRenderer": "OpenGL",
        "defaultMap": "rift.save.json",
        "tileWidth": 16,
        "tileHeight": 16,
        "defaultMapSize": { "width": 32, "height": 24 },
        "tilesets": [ "assets/tiles.png" ],
        "npcSprites": [ "assets/non-player/npc.png" ],
        "fonts": [ "assets/fonts/ui.ttf" ],
        "playerCharacters": {
            "NOT_A_CHARACTER": {
                "Walking": "assets/player/walk.png",
                "Running": "assets/player/run.png"
            }
        }
    })json");

    ManifestValidationResult result;
    std::optional<ProjectManifest> manifest = ProjectManifest::LoadFromFile(path, result);

    ASSERT_TRUE(manifest.has_value());
    EXPECT_TRUE(result.HasErrors());
    EXPECT_TRUE(HasDiagnosticFor(result, "playerCharacters.NOT_A_CHARACTER"));
}

TEST(ProjectManifestTests, MissingManifestUsesBuiltInFallbackWithWarning)
{
    TempProject project;
    ProjectManifest fallback = ProjectManifest::BuiltInFallback();
    for (const std::string& path : fallback.tilesets)
    {
        project.Touch(path);
    }
    for (const std::string& path : fallback.npcSprites)
    {
        project.Touch(path);
    }
    for (const std::string& path : fallback.fonts)
    {
        project.Touch(path);
    }
    for (const auto& [name, character] : fallback.playerCharacters)
    {
        (void)name;
        for (const auto& [spriteType, path] : character.sprites)
        {
            (void)spriteType;
            project.Touch(path);
        }
    }
    project.Touch("rift.save.json");

    CurrentPathGuard guard(project.root);
    ManifestValidationResult result;
    ProjectManifest manifest = ProjectManifest::LoadDefaultOrFallback(result);

    EXPECT_FALSE(manifest.loadedFromFile);
    EXPECT_FALSE(result.HasErrors());
    EXPECT_TRUE(result.HasWarnings());
    EXPECT_TRUE(HasDiagnosticFor(result, ProjectManifest::DEFAULT_FILENAME));
}

TEST(ProjectManifestTests, PlayerCharacterAssetsCanBeRegisteredFromManifest)
{
    TempProject project;
    CreateValidAssetFixture(project);
    std::filesystem::path path = project.WriteManifest(ValidManifestJson());

    ManifestValidationResult result;
    std::optional<ProjectManifest> manifest = ProjectManifest::LoadFromFile(path, result);
    ASSERT_TRUE(manifest.has_value());
    ASSERT_FALSE(result.HasErrors());

    auto type = EnumTraits<CharacterType>::FromString("BW1_MALE");
    ASSERT_TRUE(type.has_value());
    const auto& character = manifest->playerCharacters.at("BW1_MALE");
    for (const auto& [spriteType, spritePath] : character.sprites)
    {
        PlayerCharacter::SetCharacterAsset(
            *type, spriteType, manifest->ResolvePathString(spritePath));
    }

    SUCCEED();
}
