#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @enum ManifestDiagnosticSeverity
 * @brief Severity for project manifest parser and validator diagnostics.
 * @ingroup Core
 */
enum class ManifestDiagnosticSeverity
{
    Warning,
    Error,
};

/**
 * @struct ManifestDiagnostic
 * @brief One actionable project manifest parser or validation diagnostic.
 * @ingroup Core
 */
struct ManifestDiagnostic
{
    ManifestDiagnosticSeverity severity = ManifestDiagnosticSeverity::Warning;
    std::string fieldPath;
    std::string message;
};

/**
 * @struct ManifestValidationResult
 * @brief Collection of manifest diagnostics with convenience query helpers.
 * @ingroup Core
 */
struct ManifestValidationResult
{
    std::vector<ManifestDiagnostic> diagnostics;

    void Add(ManifestDiagnosticSeverity severity, std::string fieldPath, std::string message);
    void AddWarning(std::string fieldPath, std::string message);
    void AddError(std::string fieldPath, std::string message);

    [[nodiscard]] bool HasErrors() const;
    [[nodiscard]] bool HasWarnings() const;
};

/**
 * @struct PlayerCharacterManifest
 * @brief Sprite-sheet paths for one playable character.
 * @ingroup Core
 */
struct PlayerCharacterManifest
{
    std::map<std::string, std::string> sprites;
};

/**
 * @struct ProjectManifest
 * @brief Data-driven startup configuration for project assets and defaults.
 * @ingroup Core
 *
 * ProjectManifest describes the assets Rift needs during startup without
 * requiring users to edit Game.cpp. Relative paths are resolved against the
 * manifest file's directory.
 */
struct ProjectManifest
{
    static constexpr int CURRENT_FORMAT_VERSION = 1;
    static constexpr const char* DEFAULT_FILENAME = "rift.project.json";

    int formatVersion = CURRENT_FORMAT_VERSION;
    bool loadedFromFile = false;
    std::filesystem::path sourcePath;
    std::filesystem::path baseDirectory;

    std::string startupRenderer = "OpenGL";
    std::string defaultMap = "save.json";
    int tileWidth = 16;
    int tileHeight = 16;
    int defaultMapWidth = 125;
    int defaultMapHeight = 125;

    std::vector<std::string> tilesets;
    std::vector<std::string> npcSprites;
    std::vector<std::string> fonts;
    std::map<std::string, PlayerCharacterManifest> playerCharacters;

    [[nodiscard]] std::filesystem::path ResolvePath(const std::string& path) const;
    [[nodiscard]] std::string ResolvePathString(const std::string& path) const;
    [[nodiscard]] std::vector<std::string> ResolvePathStrings(
        const std::vector<std::string>& paths) const;

    [[nodiscard]] ManifestValidationResult Validate() const;

    static ProjectManifest BuiltInFallback();
    static std::optional<ProjectManifest> LoadFromFile(const std::filesystem::path& path,
                                                       ManifestValidationResult& result);
    static ProjectManifest LoadDefaultOrFallback(ManifestValidationResult& result);
};
