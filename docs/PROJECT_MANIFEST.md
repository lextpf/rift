# Project Manifest

Rift loads startup assets from `rift.project.json`. This keeps project-specific
paths out of `Game.cpp` and lets a new project replace sprites, tilesets, fonts,
and the default save file with data-only edits.

If `rift.project.json` is missing, Rift falls back to the built-in legacy
paths. If a manifest exists but is malformed or references missing required
assets, startup fails with diagnostics in the console.

## Lookup

At startup Rift searches for `rift.project.json` in:

1. The current working directory.
2. The current working directory's parent.
3. Built-in defaults if no manifest is found.

The first candidate that *exists* decides the outcome. If it fails to open or parse, Rift returns
the built-in defaults carrying only that parse error and never tries the parent directory, so a
malformed manifest in the working directory aborts startup instead of deferring to a valid one
further up the tree. This is deliberate: a broken project file is never silently masked by a stale
one.

Relative paths inside the manifest are resolved relative to the manifest file's
directory. The build copies the manifest next to the executable so Release and
Debug runs use the same configuration.

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Game
    participant Manifest as "ProjectManifest"
    participant FileSystem
    participant Validator

    Game->>Manifest: LoadDefaultOrFallback(result)
    Manifest->>FileSystem: Probe working directory
    alt Manifest parsed
        Manifest->>FileSystem: Parse rift.project.json
        Manifest->>Validator: Validate schema and assets
        alt Validation has errors
            Validator-->>Game: Diagnostics; startup fails
        else Valid or warnings only
            Manifest-->>Game: Loaded manifest
        end
    else Manifest exists but open/parse failed
        Manifest-->>Game: BuiltInFallback(), unvalidated, parse error only; startup fails
    else No manifest found
        Manifest->>Validator: BuiltInFallback().Validate()
        Manifest-->>Game: Built-in defaults plus bundled-asset diagnostics
    end
</pre>
\endhtmlonly

## Schema

```json
{
    "formatVersion": 1,
    "startupRenderer": "OpenGL",
    "defaultMap": "rift.save.json",
    "tileWidth": 16,
    "tileHeight": 16,
    "defaultMapSize": {
        "width": 125,
        "height": 125
    },
    "tilesets": [
        "assets/overworld/tiles.png"
    ],
    "npcSprites": [
        "assets/non-player/npc.png"
    ],
    "fonts": [
        "assets/fonts/ui.ttf"
    ],
    "particles": {
        "smoke2": "assets/particles/smoke2.png"
    },
    "playerCharacters": {
        "BW1_MALE": {
            "Walking": "assets/player/walk.png",
            "Running": "assets/player/run.png",
            "Bicycle": "assets/player/bike.png"
        }
    }
}
```

## Fields Validated as Errors

Omitting a key is always legal: every field is pre-set to the default shown in the schema, and the
reader only overwrites keys that are present. A partial manifest is therefore supported. The fields
below fail startup when they are present with an invalid value, and - for `tilesets` and the player
sprite entries - when the referenced file does not exist on disk.

Omitting a key and supplying an empty one are not the same thing. The scalar fields merge: a missing
key keeps the default. The list and table fields (`tilesets`, `npcSprites`, `fonts`, `particles`,
`playerCharacters`) replace: once the key is present with the right JSON type the default list is
discarded, so an explicitly empty array or object means "none configured", not "keep the defaults".
An empty `tilesets` array therefore fails startup. Within a list, a malformed element is reported
and skipped; the remaining elements are still read.

- `formatVersion`: must be `1` (default `1`).
- `startupRenderer`: must be `OpenGL` or `Vulkan`, matched case-insensitively, so `opengl` and
  `OPENGL` also pass (default `OpenGL`). The manifest keeps the author's casing; `RendererFactory`
  matches the name again on its own.
- `tileWidth` and `tileHeight`: must be greater than zero (default `16` each).
- `defaultMapSize`: `width` and `height` must be positive (default `125` x `125`).
- `tilesets`: at least one path, each existing on disk. No fallback exists.
- `playerCharacters`: at least one key matching a known `CharacterType`.
- `Walking` and `Running` sprites for each configured player character, each existing on disk.

Everything the engine can recover from - the map, NPC sprites, fonts, particle sprites, `Bicycle`
sheets - reports a warning instead and startup continues.

## Optional Fields

- `defaultMap`: authored/persisted map loaded by Continue, used as the New Game
  baseline, and written by the editor. Missing files are allowed; Rift generates
  a default map using `defaultMapSize`.
- `defaultMapSize`: used only when the default map cannot be loaded.
- `npcSprites`: used by editor NPC placement and map-load type lookup.
- `fonts`: project fonts tried before renderer system fallbacks.
- `particles`: particle sprite name to asset path. The path links one on-disk file; the particle
  atlas derives the animated `_strip` sibling from it, so either the static frame or the strip may
  be linked. An empty table or a missing file warns and that particle falls back to a procedural
  shape.
- `Bicycle`: optional player sprite; missing files warn but do not block startup.

## Troubleshooting

Common diagnostics:

| Message | Meaning |
|---------|---------|
| `Missing tileset asset` | A required tileset path does not exist relative to the manifest. |
| `Unknown CharacterType name` | The player character key does not match `CharacterType` names such as `BW1_MALE`. |
| `Default map was not found` | Startup will generate a map and editor saves will write to the configured path. |
| `No project font candidates configured` | Text rendering falls back to renderer defaults and system fonts. |
| `Missing particle sprite` | A `particles` entry points at a file that does not exist; that particle draws as a procedural shape. |

Use forward slashes in JSON paths. Absolute paths work, but relative project
paths are easier to share.
