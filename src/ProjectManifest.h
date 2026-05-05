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
    Warning,  ///< Non-fatal issue; startup may continue with fallback behavior.
    Error,    ///< Fatal manifest issue; startup should stop.
};

/**
 * @struct ManifestDiagnostic
 * @brief One actionable project manifest parser or validation diagnostic.
 * @ingroup Core
 */
struct ManifestDiagnostic
{
    ManifestDiagnosticSeverity severity =
        ManifestDiagnosticSeverity::Warning;  ///< Diagnostic level.
    std::string fieldPath;  ///< JSON field path or asset category that triggered the diagnostic.
    std::string message;    ///< Human-readable explanation.
};

/**
 * @struct ManifestValidationResult
 * @brief Collection of manifest diagnostics with convenience query helpers.
 * @ingroup Core
 */
struct ManifestValidationResult
{
    std::vector<ManifestDiagnostic> diagnostics;  ///< Ordered parser and validation diagnostics.

    /// @brief Append a diagnostic with explicit severity.
    void Add(ManifestDiagnosticSeverity severity, std::string fieldPath, std::string message);
    /// @brief Append a non-fatal warning diagnostic.
    void AddWarning(std::string fieldPath, std::string message);
    /// @brief Append a fatal error diagnostic.
    void AddError(std::string fieldPath, std::string message);

    /// @return True if any diagnostic has Error severity.
    [[nodiscard]] bool HasErrors() const;
    /// @return True if any diagnostic has Warning severity.
    [[nodiscard]] bool HasWarnings() const;
};

/**
 * @struct PlayerCharacterManifest
 * @brief Sprite-sheet paths for one playable character.
 * @ingroup Core
 */
struct PlayerCharacterManifest
{
    std::map<std::string, std::string> sprites;  ///< Sprite role name to relative asset path.
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
    static constexpr int CURRENT_FORMAT_VERSION = 1;  ///< Only supported manifest schema version.
    static constexpr const char* DEFAULT_FILENAME =
        "rift.project.json";  ///< Startup manifest name.

    int formatVersion = CURRENT_FORMAT_VERSION;  ///< Parsed schema version.
    bool loadedFromFile = false;                 ///< True when loaded from a JSON file.
    std::filesystem::path sourcePath;            ///< Manifest file path, if loaded.
    std::filesystem::path baseDirectory;         ///< Directory used to resolve relative paths.

    std::string startupRenderer = "OpenGL";     ///< Requested renderer name: "OpenGL" or "Vulkan".
    std::string defaultMap = "rift.save.json";  ///< Save/map path used at startup and editor save.
    int tileWidth = 16;                         ///< Tile width in pixels; must be positive.
    int tileHeight = 16;                        ///< Tile height in pixels; must be positive.
    int defaultMapWidth = 125;                  ///< Generated map width when no save exists.
    int defaultMapHeight = 125;                 ///< Generated map height when no save exists.

    std::vector<std::string> tilesets;    ///< Required tileset image paths.
    std::vector<std::string> npcSprites;  ///< NPC sprite paths used by map loading and editor.
    std::vector<std::string> fonts;       ///< Project font candidates tried before fallbacks.
    std::map<std::string, PlayerCharacterManifest> playerCharacters;  ///< CharacterType to sprites.

    /// @brief Resolve a manifest-relative path against baseDirectory.
    /// @return Absolute or base-directory-relative filesystem path.
    [[nodiscard]] std::filesystem::path ResolvePath(const std::string& path) const;
    /// @brief Resolve a path and return it as a string for legacy call sites.
    [[nodiscard]] std::string ResolvePathString(const std::string& path) const;
    /// @brief Resolve every path in a vector.
    [[nodiscard]] std::vector<std::string> ResolvePathStrings(
        const std::vector<std::string>& paths) const;

    /// @brief Validate schema values and referenced assets.
    /// @return Diagnostics; errors indicate startup should fail.
    [[nodiscard]] ManifestValidationResult Validate() const;

    /// @brief Build the legacy asset/default-map configuration used when no manifest exists.
    static ProjectManifest BuiltInFallback();
    /// @brief Parse one manifest file and append parse/validation diagnostics.
    /// @return Parsed manifest when JSON was readable; nullopt on parse/open failure.
    static std::optional<ProjectManifest> LoadFromFile(const std::filesystem::path& path,
                                                       ManifestValidationResult& result);
    /// @brief Search standard locations, load a manifest, or return BuiltInFallback().
    static ProjectManifest LoadDefaultOrFallback(ManifestValidationResult& result);
};
