#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @enum ManifestDiagnosticSeverity
 * @brief Severity for project manifest parser and validator diagnostics.
 * @author Alex (https://github.com/lextpf)
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
 * @author Alex (https://github.com/lextpf)
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
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Accumulates across a whole load: LoadFromFile appends its own parse diagnostics and then
 * the full Validate() run, so one result can carry both. Nothing is ever removed.
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

    /// @brief Whether startup should be aborted.
    /// @return True if any diagnostic has Error severity.
    [[nodiscard]] bool HasErrors() const;
    /// @brief Whether anything non-fatal was reported.
    /// @return True if any diagnostic has Warning severity.
    [[nodiscard]] bool HasWarnings() const;
};

/**
 * @struct PlayerCharacterManifest
 * @brief Sprite-sheet paths for one playable character.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Validate() requires the "Walking" and "Running" roles and treats "Bicycle" as optional;
 * any other key is carried through without complaint.
 */
struct PlayerCharacterManifest
{
    std::map<std::string, std::string> sprites;  ///< Sprite role name to relative asset path.

    PlayerCharacterManifest() = default;
    PlayerCharacterManifest(const PlayerCharacterManifest&) = default;
    PlayerCharacterManifest(PlayerCharacterManifest&&) noexcept = default;
    PlayerCharacterManifest& operator=(const PlayerCharacterManifest&) = default;
    PlayerCharacterManifest& operator=(PlayerCharacterManifest&&) noexcept = default;
};

/**
 * @struct ProjectManifest
 * @brief Data-driven startup configuration for project assets and defaults.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * ProjectManifest describes the assets Rift needs during startup without
 * requiring users to edit Game.cpp. Relative paths are resolved against the
 * manifest file's directory (@ref baseDirectory), or against the process working
 * directory when the manifest was not loaded from a file.
 *
 * Every field below is pre-set to the value it keeps when the corresponding JSON key is
 * absent, so a partial manifest is legal: parsing only overwrites keys that are present.
 *
 * @par Accepted document shape
 * Two keys do not match their field names: @c defaultMapSize feeds @ref defaultMapWidth /
 * @ref defaultMapHeight, and @c particles feeds @ref particleSprites.
 * @code{.json}
 * {
 *   "formatVersion": 1,
 *   "startupRenderer": "OpenGL",
 *   "defaultMap": "rift.save.json",
 *   "tileWidth": 16,
 *   "tileHeight": 16,
 *   "defaultMapSize": { "width": 125, "height": 125 },
 *   "tilesets": ["assets/tiles/outdoor.png"],
 *   "npcSprites": ["assets/non-player/guard.png"],
 *   "fonts": ["assets/fonts/pixel.ttf"],
 *   "particles": { "smoke2": "assets/particles/smoke2.png" },
 *   "playerCharacters": {
 *     "BW1_MALE": { "Walking": "...", "Running": "...", "Bicycle": "..." }
 *   }
 * }
 * @endcode
 * The scalars are overwritten only when their key is present. The list and object fields
 * are replaced wholesale, so an explicitly empty one means "none", not "keep the defaults".
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

    /// Requested renderer name, "OpenGL" or "Vulkan". Matched case-insensitively, and
    /// stored with the author's casing rather than normalized.
    std::string startupRenderer = "OpenGL";
    std::string defaultMap = "rift.save.json";  ///< Save/map path used at startup and editor save.
    int tileWidth = 16;                         ///< Tile width in pixels; must be positive.
    int tileHeight = 16;                        ///< Tile height in pixels; must be positive.
    /// Generated map width when no save exists. Read from `defaultMapSize.width`.
    int defaultMapWidth = 125;
    /// Generated map height when no save exists. Read from `defaultMapSize.height`.
    int defaultMapHeight = 125;

    std::vector<std::string> tilesets;    ///< Required tileset image paths.
    std::vector<std::string> npcSprites;  ///< NPC sprite paths used by map loading and editor.
    std::vector<std::string> fonts;       ///< Project font candidates tried before fallbacks.
    std::map<std::string, PlayerCharacterManifest> playerCharacters;  ///< CharacterType to sprites.

    /**
     * @brief Particle sprite name (e.g. "smoke2") to relative asset path.
     *
     * Read from the manifest's `particles` object; the JSON key does not match this field
     * name. The path links one on-disk file; the particle atlas derives the animated "_strip"
     * sibling from it, so either the static frame or the strip may be linked. Missing
     * entries degrade to procedural fallback sprites.
     */
    std::map<std::string, std::string> particleSprites;

    ProjectManifest() = default;
    ProjectManifest(const ProjectManifest&) = default;
    ProjectManifest(ProjectManifest&&) noexcept = default;
    ProjectManifest& operator=(const ProjectManifest&) = default;
    ProjectManifest& operator=(ProjectManifest&&) noexcept = default;

    /**
     * @brief Resolve a manifest-relative path against baseDirectory.
     *
     * Purely lexical: it never touches the filesystem, so it also succeeds for a path that
     * does not exist yet - which is what lets callers resolve a save file before writing it.
     *
     * @param path  Absolute path, or one relative to @ref baseDirectory.
     * @return      Lexically normalized path. An absolute input is returned normalized; a
     *              relative one is joined onto baseDirectory. Every construction path leaves
     *              baseDirectory absolute, so in practice the result is absolute.
     */
    [[nodiscard]] std::filesystem::path ResolvePath(const std::string& path) const;
    /// @brief Resolve a path and return it as a string for legacy call sites.
    [[nodiscard]] std::string ResolvePathString(const std::string& path) const;
    /// @brief Resolve every path in a vector.
    [[nodiscard]] std::vector<std::string> ResolvePathStrings(
        const std::vector<std::string>& paths) const;

    /**
     * @brief Validate schema values and referenced assets.
     *
     * Touches the filesystem: every referenced path is resolved and existence-checked.
     * Errors are reserved for things startup genuinely cannot proceed without: format
     * version, renderer name, tile/map dimensions, a non-empty tileset list with every
     * listed tileset present on disk, and at least one player character, each one naming a
     * real CharacterType and having Walking and Running sprites that exist. Both lists are
     * all-of, not any-of - one stale path in an otherwise good list is fatal. Everything
     * optional - a missing default map, NPC sprites, fonts, particle sprites, Bicycle
     * sprites - degrades to a warning because the engine has a fallback for it. Fonts are
     * the one genuine any-of list: each missing candidate warns, but only an all-missing
     * list means the renderer's own font fallback applies.
     *
     * @return Diagnostics; errors indicate startup should fail.
     */
    [[nodiscard]] ManifestValidationResult Validate() const;

    /**
     * @brief Build the legacy asset/default-map configuration used when no manifest exists.
     *
     * baseDirectory is set to the process working directory, so the built-in paths resolve
     * relative to wherever the game was started. This does not validate what it builds -
     * callers that care whether the bundled assets are present run @ref Validate.
     */
    static ProjectManifest BuiltInFallback();

    /**
     * @brief Parse one manifest file and append parse/validation diagnostics.
     *
     * On success the returned manifest has already been passed through @ref Validate and
     * its diagnostics appended to @p result, so callers do not validate again. A returned
     * value therefore does not mean the manifest is usable - check `result.HasErrors()`.
     *
     * @param path   Manifest file to read; also becomes sourcePath / baseDirectory.
     * @param result Diagnostic sink, appended to (never cleared).
     * @return Parsed manifest when JSON was readable; nullopt on parse/open failure.
     */
    static std::optional<ProjectManifest> LoadFromFile(const std::filesystem::path& path,
                                                       ManifestValidationResult& result);

    /**
     * @brief Search standard locations, load a manifest, or return BuiltInFallback().
     *
     * Candidates are tried in order: `&lt;cwd&gt;/rift.project.json`, then the same name in
     * the parent directory (which covers running the binary out of a build subdirectory).
     * The first candidate that exists on disk decides the outcome - a later candidate is
     * never consulted, even when that first file fails to parse.
     *
     * The two fallback paths differ: when no candidate exists at all, the built-in manifest
     * is validated and its diagnostics appended (so missing bundled assets are reported);
     * when a candidate existed but failed to parse, the built-in manifest is returned
     * unvalidated, carrying only the parse error.
     *
     * @param result Diagnostic sink, appended to (never cleared).
     * @return The loaded manifest, or BuiltInFallback() when none was found or parsed.
     */
    static ProjectManifest LoadDefaultOrFallback(ManifestValidationResult& result);
};
