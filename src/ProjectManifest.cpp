// ProjectManifest - reader and validator for rift.project.json.
//
// Load pipeline, top to bottom:
//
//   LoadDefaultOrFallback()
//     -> DefaultManifestCandidates()   cwd, then parent dir
//     -> first candidate that exists decides; later ones are never tried
//          |- LoadFromFile()  parse -> Read*Field helpers -> Validate()
//          |     |- parse/open failure -> BuiltInFallback(), unvalidated
//          |     '- success            -> manifest + its validation diagnostics
//          '- no candidate exists      -> BuiltInFallback(), validated
//
// Two rules shape the whole file:
//
//   1. Absent keys are not errors. Every Read* helper returns early when the key is
//      missing, leaving the field at the default the header already assigned. Only a key
//      present with the wrong JSON type is an error. That is what makes partial manifests
//      legal.
//   2. Severity follows recoverability. Validate() raises an Error only where the engine
//      has no fallback (schema version, renderer name, tile/map size, tilesets, player
//      characters and their Walking/Running sprites); everything with a fallback path -
//      map, NPC sprites, fonts, particle sprites, Bicycle sprites - is a Warning.
//
// Adding a manifest field means touching four places: the member + its default in
// ProjectManifest.hpp, a Read* call in LoadFromFile, a rule in Validate, and - if the
// built-in defaults should cover it - BuiltInFallback.

#include "ProjectManifest.hpp"

#include "CharacterType.hpp"
#include "EnumTraits.hpp"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <ranges>

namespace
{
using json = nlohmann::json;

// Asset paths baked into BuiltInFallback(), used when no manifest file is found. They are
// relative, so they resolve against the process working directory.
constexpr const char* kDefaultTilesets[] = {
    "assets/overworld/cb5fa6a6-f88d-47ca-95d6-c73cc79f879d.png",
    "assets/overworld/5ee53950-ea54-41c5-93d3-991e1407cb8b.png",
    "assets/overworld/fd3ff88b-f533-4d40-947c-2c7e5e90839c.png",
    "assets/overworld/11941f71-5703-4a5b-b167-9cd53f88e10e.png",
    "assets/overworld/2b0922a6-66f8-4137-89af-45aaabc5434f.png",
    "assets/overworld/40954708-5e64-4179-8faa-3bd3068de66c.png",
    "assets/overworld/1bc8e647-5e22-4456-839a-845991ba4255.png",
    "assets/overworld/145bb27c-c01d-44fd-b820-2f36f37673f2.png",
    "assets/overworld/6a913092-f773-4d2f-a5d7-09a8d9fbb401.png",
};

constexpr const char* kDefaultNpcSprites[] = {
    "assets/non-player/f8cb6fd1-b8a5-44df-b017-c6cc9834353f.png",
    "assets/non-player/ccdc6c30-ecf8-4d08-b5ef-1307d84eecf0.png",
    "assets/non-player/8eb301d1-1dd4-4044-8718-72de1e7b981b.png",
    "assets/non-player/5a5f49f1-32be-4645-b5ca-6c0817461253.png",
    "assets/non-player/d06a4775-e373-4c7a-acfb-6b8fe5f01ca1.png",
    "assets/non-player/908fc99d-b456-45a2-937c-074413e8f664.png",
    "assets/non-player/f7e4604c-a458-4096-bbba-59149419c650.png",
    "assets/non-player/94c6b5b9-99fa-4f3d-bab5-b93684c934e5.png",
};

constexpr const char* kDefaultFont = "assets/fonts/c8ab67e0-519a-49b5-b693-e8fc86d08efa.ttf";

// Register one built-in playable character. characterName must be a CharacterType enum
// name - Validate() rejects anything EnumTraits<CharacterType> cannot parse - and the three
// sprite roles are exactly the keys Validate() looks for.
void AddPlayer(ProjectManifest& manifest,
               std::string characterName,
               std::string walking,
               std::string running,
               std::string bicycle)
{
    PlayerCharacterManifest character;
    character.sprites["Walking"] = std::move(walking);
    character.sprites["Running"] = std::move(running);
    character.sprites["Bicycle"] = std::move(bicycle);
    manifest.playerCharacters[std::move(characterName)] = std::move(character);
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Renderer names are matched case-insensitively, so "OpenGL", "opengl" and "OPENGL" all
// pass validation. The stored string keeps the author's casing; RendererFactory does its
// own matching.
bool IsKnownRendererName(const std::string& name)
{
    std::string lower = ToLower(name);
    return lower == "opengl" || lower == "vulkan";
}

// Existence check against the manifest-relative resolution rules. An empty path counts as
// missing, which is what lets the validators treat "" and "file not there" identically.
bool PathExists(const ProjectManifest& manifest, const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
    return std::filesystem::exists(manifest.ResolvePath(path));
}

// Read helpers. All four share one contract: a missing key leaves `out` untouched (the
// header's default survives), while a key of the wrong JSON type is an Error and also
// leaves `out` untouched. Nothing here throws - the caller has already established that
// the document parsed and is an object.
void ReadStringField(const json& object,
                     const char* key,
                     std::string& out,
                     ManifestValidationResult& result)
{
    if (!object.contains(key))
    {
        return;
    }
    if (!object[key].is_string())
    {
        result.AddError(key, "Expected a string.");
        return;
    }
    out = object[key].get<std::string>();
}

void ReadIntField(const json& object, const char* key, int& out, ManifestValidationResult& result)
{
    if (!object.contains(key))
    {
        return;
    }
    if (!object[key].is_number_integer())
    {
        result.AddError(key, "Expected an integer.");
        return;
    }
    out = object[key].get<int>();
}

// Unlike the scalar readers, the array/map readers replace rather than merge: `out` is
// cleared as soon as the key is confirmed to be the right type, so an explicitly empty
// array in the manifest means "none", not "keep the defaults". Individual bad elements are
// reported and skipped, so one malformed entry does not discard the rest of the list.
void ReadStringArray(const json& object,
                     const char* key,
                     std::vector<std::string>& out,
                     ManifestValidationResult& result)
{
    if (!object.contains(key))
    {
        return;
    }
    if (!object[key].is_array())
    {
        result.AddError(key, "Expected an array of strings.");
        return;
    }

    out.clear();
    const auto& array = object[key];
    for (size_t i = 0; i < array.size(); ++i)
    {
        if (!array[i].is_string())
        {
            result.AddError(std::string(key) + "[" + std::to_string(i) + "]",
                            "Expected a string path.");
            continue;
        }
        out.push_back(array[i].get<std::string>());
    }
}

// Name-to-path object reader (used for the "particles" table). Same replace-not-merge and
// skip-bad-element behavior as ReadStringArray.
void ReadStringMap(const json& object,
                   const char* key,
                   std::map<std::string, std::string>& out,
                   ManifestValidationResult& result)
{
    if (!object.contains(key))
    {
        return;
    }
    if (!object[key].is_object())
    {
        result.AddError(key, "Expected an object of name-to-path strings.");
        return;
    }

    out.clear();
    for (const auto& [name, valueJson] : object[key].items())
    {
        if (!valueJson.is_string())
        {
            result.AddError(std::string(key) + "." + name, "Expected a string path.");
            continue;
        }
        out[name] = valueJson.get<std::string>();
    }
}

// Two-level reader for "playerCharacters": { "<CharacterType>": { "<role>": "<path>" } }.
// Deliberately permissive about the inner keys - any sprite role is accepted here and the
// required set (Walking / Running, optional Bicycle) is enforced later by Validate, which
// also checks that the outer key names a real CharacterType. A malformed character or
// sprite entry is reported and skipped, not fatal to the whole table.
void ReadPlayerCharacters(const json& object,
                          ProjectManifest& manifest,
                          ManifestValidationResult& result)
{
    if (!object.contains("playerCharacters"))
    {
        return;
    }
    if (!object["playerCharacters"].is_object())
    {
        result.AddError("playerCharacters", "Expected an object keyed by character name.");
        return;
    }

    manifest.playerCharacters.clear();
    for (const auto& [characterName, characterJson] : object["playerCharacters"].items())
    {
        if (!characterJson.is_object())
        {
            result.AddError("playerCharacters." + characterName,
                            "Expected an object of sprite-type paths.");
            continue;
        }

        PlayerCharacterManifest character;
        for (const auto& [spriteType, spriteJson] : characterJson.items())
        {
            if (!spriteJson.is_string())
            {
                result.AddError("playerCharacters." + characterName + "." + spriteType,
                                "Expected a string path.");
                continue;
            }
            character.sprites[spriteType] = spriteJson.get<std::string>();
        }
        manifest.playerCharacters[characterName] = std::move(character);
    }
}

// Where LoadDefaultOrFallback looks, in priority order: the working directory first, then
// its parent. The parent entry is what lets the game find the repo-root manifest when it is
// launched from a build output directory. Existence is not checked here.
std::vector<std::filesystem::path> DefaultManifestCandidates()
{
    std::vector<std::filesystem::path> candidates;
    std::filesystem::path cwd = std::filesystem::current_path();
    candidates.push_back(cwd / ProjectManifest::DEFAULT_FILENAME);

    if (cwd.has_parent_path())
    {
        candidates.push_back(cwd.parent_path() / ProjectManifest::DEFAULT_FILENAME);
    }

    return candidates;
}
}  // namespace

void ManifestValidationResult::Add(ManifestDiagnosticSeverity severity,
                                   std::string fieldPath,
                                   std::string message)
{
    diagnostics.push_back({severity, std::move(fieldPath), std::move(message)});
}

void ManifestValidationResult::AddWarning(std::string fieldPath, std::string message)
{
    Add(ManifestDiagnosticSeverity::Warning, std::move(fieldPath), std::move(message));
}

void ManifestValidationResult::AddError(std::string fieldPath, std::string message)
{
    Add(ManifestDiagnosticSeverity::Error, std::move(fieldPath), std::move(message));
}

bool ManifestValidationResult::HasErrors() const
{
    return std::ranges::any_of(
        diagnostics,
        [](const ManifestDiagnostic& diagnostic)
        { return diagnostic.severity == ManifestDiagnosticSeverity::Error; });
}

bool ManifestValidationResult::HasWarnings() const
{
    return std::ranges::any_of(
        diagnostics,
        [](const ManifestDiagnostic& diagnostic)
        { return diagnostic.severity == ManifestDiagnosticSeverity::Warning; });
}

// Absolute paths are taken as-is; relative ones hang off the manifest's own directory so a
// project can be moved without rewriting every path. baseDirectory is empty only for a
// default-constructed manifest, in which case the working directory stands in.
// lexically_normal (not canonical) keeps this a pure string operation: it never touches the
// filesystem and never fails on a path that does not exist yet.
std::filesystem::path ProjectManifest::ResolvePath(const std::string& path) const
{
    std::filesystem::path resolved(path);
    if (resolved.is_absolute())
    {
        return resolved.lexically_normal();
    }

    std::filesystem::path base =
        baseDirectory.empty() ? std::filesystem::current_path() : baseDirectory;
    return (base / resolved).lexically_normal();
}

std::string ProjectManifest::ResolvePathString(const std::string& path) const
{
    return ResolvePath(path).string();
}

// Bulk form for the asset lists (tilesets, npcSprites, fonts). Order is preserved, which
// matters for fonts: the loader takes the first candidate that works.
std::vector<std::string> ProjectManifest::ResolvePathStrings(
    const std::vector<std::string>& paths) const
{
    std::vector<std::string> resolved;
    resolved.reserve(paths.size());
    for (const std::string& path : paths)
    {
        resolved.push_back(ResolvePathString(path));
    }
    return resolved;
}

// Full schema + asset audit, in fixed order: schema, renderer, geometry, tilesets, map,
// NPC sprites, fonts, particles, player characters. Returns a fresh result rather than
// appending to a caller's, so it can be run standalone; LoadFromFile splices the output
// into its own diagnostic list.
//
// Every path check goes through PathExists, so this hits the filesystem once per referenced
// asset. Error vs Warning is the recoverability rule from the top of the file: if startup
// has a working fallback for the missing thing, it is a Warning.
ManifestValidationResult ProjectManifest::Validate() const
{
    ManifestValidationResult result;

    // Schema gate first - a version this build does not understand makes every field below
    // suspect, but validation still continues so the report is complete in one pass.
    if (formatVersion != CURRENT_FORMAT_VERSION)
    {
        result.AddError("formatVersion",
                        "Unsupported manifest format version " + std::to_string(formatVersion) +
                            "; expected " + std::to_string(CURRENT_FORMAT_VERSION) + ".");
    }

    if (!IsKnownRendererName(startupRenderer))
    {
        result.AddError("startupRenderer", "Expected OpenGL or Vulkan.");
    }

    if (tileWidth <= 0)
    {
        result.AddError("tileWidth", "Tile width must be greater than zero.");
    }
    if (tileHeight <= 0)
    {
        result.AddError("tileHeight", "Tile height must be greater than zero.");
    }
    if (defaultMapWidth <= 0 || defaultMapHeight <= 0)
    {
        result.AddError("defaultMapSize",
                        "Default map width and height must be greater than zero.");
    }

    // Tilesets are the one asset class with no fallback - without them the tilemap has
    // nothing to draw - so both "none configured" and "configured but missing" are Errors.
    if (tilesets.empty())
    {
        result.AddError("tilesets", "At least one tileset path is required.");
    }
    for (size_t i = 0; i < tilesets.size(); ++i)
    {
        if (!PathExists(*this, tilesets[i]))
        {
            result.AddError("tilesets[" + std::to_string(i) + "]",
                            "Missing tileset asset: " + ResolvePathString(tilesets[i]));
        }
    }

    // A missing save/map file is normal on a first run - startup generates a default map -
    // so neither branch is fatal.
    if (defaultMap.empty())
    {
        result.AddWarning("defaultMap",
                          "No default map configured; startup will generate the default map.");
    }
    else if (!PathExists(*this, defaultMap))
    {
        result.AddWarning("defaultMap",
                          "Default map was not found; startup will generate the default map: " +
                              ResolvePathString(defaultMap));
    }

    if (npcSprites.empty())
    {
        result.AddWarning("npcSprites",
                          "No NPC sprites configured; editor NPC placement is empty.");
    }
    for (size_t i = 0; i < npcSprites.size(); ++i)
    {
        if (!PathExists(*this, npcSprites[i]))
        {
            result.AddWarning("npcSprites[" + std::to_string(i) + "]",
                              "Missing NPC sprite: " + ResolvePathString(npcSprites[i]));
        }
    }

    if (fonts.empty())
    {
        result.AddWarning("fonts",
                          "No project font candidates configured; renderer fallback applies.");
    }
    else
    {
        // Fonts are a candidate LIST, not a required set: the first one that loads wins.
        // So each missing entry is worth a warning, but only an all-missing list warrants
        // the second, louder warning that the renderer's own fallback will be used.
        bool anyFontExists = false;
        for (size_t i = 0; i < fonts.size(); ++i)
        {
            if (PathExists(*this, fonts[i]))
            {
                anyFontExists = true;
            }
            else
            {
                result.AddWarning("fonts[" + std::to_string(i) + "]",
                                  "Missing font candidate: " + ResolvePathString(fonts[i]));
            }
        }
        if (!anyFontExists)
        {
            result.AddWarning("fonts",
                              "No configured project fonts exist; renderer fallback applies.");
        }
    }

    if (particleSprites.empty())
    {
        result.AddWarning("particles",
                          "No particle sprites configured; particle rendering falls back to "
                          "procedural shapes.");
    }
    for (const auto& [particleName, particlePath] : particleSprites)
    {
        if (!PathExists(*this, particlePath))
        {
            result.AddWarning("particles." + particleName,
                              "Missing particle sprite: " + ResolvePathString(particlePath));
        }
    }

    // The player must be drawable, so this block is the other source of hard Errors: an
    // unknown CharacterType key, or a Walking/Running sprite that is unset or missing on
    // disk. Bicycle is a movement mode the game can omit, so a missing sheet only warns.
    if (playerCharacters.empty())
    {
        result.AddError("playerCharacters", "At least one player character is required.");
    }

    for (const auto& [characterName, character] : playerCharacters)
    {
        if (!EnumTraits<CharacterType>::FromString(characterName).has_value())
        {
            result.AddError("playerCharacters." + characterName,
                            "Unknown CharacterType name: " + characterName);
        }

        const auto walking = character.sprites.find("Walking");
        const auto running = character.sprites.find("Running");
        const auto bicycle = character.sprites.find("Bicycle");

        if (walking == character.sprites.end() || walking->second.empty())
        {
            result.AddError("playerCharacters." + characterName + ".Walking",
                            "Walking sprite is required.");
        }
        else if (!PathExists(*this, walking->second))
        {
            result.AddError("playerCharacters." + characterName + ".Walking",
                            "Missing Walking sprite: " + ResolvePathString(walking->second));
        }

        if (running == character.sprites.end() || running->second.empty())
        {
            result.AddError("playerCharacters." + characterName + ".Running",
                            "Running sprite is required.");
        }
        else if (!PathExists(*this, running->second))
        {
            result.AddError("playerCharacters." + characterName + ".Running",
                            "Missing Running sprite: " + ResolvePathString(running->second));
        }

        if (bicycle == character.sprites.end() || bicycle->second.empty())
        {
            result.AddWarning("playerCharacters." + characterName + ".Bicycle",
                              "Bicycle sprite is optional but not configured.");
        }
        else if (!PathExists(*this, bicycle->second))
        {
            result.AddWarning("playerCharacters." + characterName + ".Bicycle",
                              "Missing Bicycle sprite: " + ResolvePathString(bicycle->second));
        }
    }

    return result;
}

// The pre-manifest configuration, kept so the game still boots with the bundled assets when
// rift.project.json is absent. Scalars (renderer, tile size, map size, defaultMap) are left
// at the header's defaults; only the asset lists are filled in. Nothing here is validated -
// see LoadDefaultOrFallback for which of its callers does that.
ProjectManifest ProjectManifest::BuiltInFallback()
{
    ProjectManifest manifest;
    manifest.loadedFromFile = false;
    // No source file, so relative asset paths resolve against the working directory.
    manifest.baseDirectory = std::filesystem::current_path();

    manifest.tilesets.assign(std::begin(kDefaultTilesets), std::end(kDefaultTilesets));
    manifest.npcSprites.assign(std::begin(kDefaultNpcSprites), std::end(kDefaultNpcSprites));
    manifest.fonts.push_back(kDefaultFont);

    AddPlayer(manifest,
              "BW1_MALE",
              "assets/player/1135c14b-d3cb-414e-8b87-8dca516ba610.png",
              "assets/player/2444a0be-9d2a-4b12-9921-4ca1956e7107.png",
              "assets/player/e6b68c46-ab34-4dbb-bca0-93710e3a433c.png");
    AddPlayer(manifest,
              "BW1_FEMALE",
              "assets/player/5f3431e3-4835-4266-af9c-505b771122ee.png",
              "assets/player/e2216c65-fef8-41c9-a5b8-911a962d7ae2.png",
              "assets/player/9ba37d2a-fe59-4fee-86d5-ca1e17bca11f.png");
    AddPlayer(manifest,
              "BW2_MALE",
              "assets/player/f3a3f051-382e-4653-8449-131d2a75548e.png",
              "assets/player/b67d0c3e-b2d1-48bc-b0a9-2ea5a42037c8.png",
              "assets/player/1023c322-8f93-4f73-8772-7543bf832569.png");
    AddPlayer(manifest,
              "BW2_FEMALE",
              "assets/player/1ce93276-4959-476f-adeb-508c86802567.png",
              "assets/player/2f1d4723-c682-4d21-9991-af4f3513bdc1.png",
              "assets/player/980d60d7-3bbc-4c1f-9681-5b7f371d4605.png");
    AddPlayer(manifest,
              "CC_FEMALE",
              "assets/player/17d3da80-9b85-42e5-adf8-fd5823962f20.png",
              "assets/player/2f079f34-3ea2-4c6a-a054-de5ba9c44e2f.png",
              "assets/player/e23ea083-b992-42dd-8dd2-690f246bc164.png");

    return manifest;
}

// Parse one manifest file. nullopt is reserved for "there is no usable document at all"
// (cannot open, malformed JSON, root is not an object); every other problem becomes a
// diagnostic on a manifest that is still returned. So a non-null return does not mean the
// manifest is good - callers must inspect result.HasErrors().
std::optional<ProjectManifest> ProjectManifest::LoadFromFile(const std::filesystem::path& path,
                                                             ManifestValidationResult& result)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        result.AddError(path.string(), "Could not open project manifest.");
        return std::nullopt;
    }

    json document;
    try
    {
        file >> document;
    }
    catch (const json::exception& e)
    {
        result.AddError(path.string(), std::string("Malformed project manifest JSON: ") + e.what());
        return std::nullopt;
    }

    if (!document.is_object())
    {
        result.AddError(path.string(), "Project manifest root must be a JSON object.");
        return std::nullopt;
    }

    ProjectManifest manifest;
    manifest.loadedFromFile = true;
    // Absolute first: baseDirectory must stay valid even if the working directory changes
    // later, since every ResolvePath call hangs off it.
    manifest.sourcePath = std::filesystem::absolute(path).lexically_normal();
    manifest.baseDirectory = manifest.sourcePath.parent_path();

    // Field reads. Order is cosmetic - each helper is independent and skips absent keys, so
    // a new field can be appended anywhere in this block.
    ReadIntField(document, "formatVersion", manifest.formatVersion, result);
    ReadStringField(document, "startupRenderer", manifest.startupRenderer, result);
    ReadStringField(document, "defaultMap", manifest.defaultMap, result);
    ReadIntField(document, "tileWidth", manifest.tileWidth, result);
    ReadIntField(document, "tileHeight", manifest.tileHeight, result);

    // "defaultMapSize" is the only nested object among the scalars, so it gets an inline
    // shape check before its two members are read with the ordinary helper.
    if (document.contains("defaultMapSize"))
    {
        if (!document["defaultMapSize"].is_object())
        {
            result.AddError("defaultMapSize", "Expected an object with width and height.");
        }
        else
        {
            ReadIntField(document["defaultMapSize"], "width", manifest.defaultMapWidth, result);
            ReadIntField(document["defaultMapSize"], "height", manifest.defaultMapHeight, result);
        }
    }

    ReadStringArray(document, "tilesets", manifest.tilesets, result);
    ReadStringArray(document, "npcSprites", manifest.npcSprites, result);
    ReadStringArray(document, "fonts", manifest.fonts, result);
    ReadStringMap(document, "particles", manifest.particleSprites, result);
    ReadPlayerCharacters(document, manifest, result);

    // Validate here rather than at the call site so no caller can forget. Diagnostics are
    // appended to the caller's list, keeping parse errors and validation findings in one
    // ordered report.
    ManifestValidationResult validation = manifest.Validate();
    result.diagnostics.insert(
        result.diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());

    return manifest;
}

// Search order, then fallback. The first candidate that exists is authoritative: if it is
// there but unreadable, the loader falls back rather than trying the next location, so a broken
// project file is never silently masked by a stale one further up the tree.
ProjectManifest ProjectManifest::LoadDefaultOrFallback(ManifestValidationResult& result)
{
    for (const auto& candidate : DefaultManifestCandidates())
    {
        if (!std::filesystem::exists(candidate))
        {
            continue;
        }

        std::optional<ProjectManifest> manifest = LoadFromFile(candidate, result);
        if (manifest.has_value())
        {
            return *manifest;
        }

        // Unreadable manifest: return the built-ins carrying only the parse error. Note the
        // asymmetry with the not-found path below - this fallback is not validated, so the
        // report contains the parse failure and nothing about the built-in assets.
        return BuiltInFallback();
    }

    // No manifest anywhere: warn, then fall back and validate, so the caller still learns
    // whether the bundled assets are present relative to the working directory.
    result.AddWarning(DEFAULT_FILENAME,
                      "Project manifest not found; using built-in asset defaults.");
    ProjectManifest fallback = BuiltInFallback();
    ManifestValidationResult validation = fallback.Validate();
    result.diagnostics.insert(
        result.diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
    return fallback;
}
