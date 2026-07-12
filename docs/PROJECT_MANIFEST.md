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
    alt Manifest found
        Manifest->>FileSystem: Parse rift.project.json
        Manifest->>Validator: Validate schema and assets
        alt Validation has errors
            Validator-->>Game: Diagnostics; startup fails
        else Valid or warnings only
            Manifest-->>Game: Loaded manifest
        end
    else No manifest found
        Manifest-->>Game: BuiltInFallback()
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
    "playerCharacters": {
        "BW1_MALE": {
            "Walking": "assets/player/walk.png",
            "Running": "assets/player/run.png",
            "Bicycle": "assets/player/bike.png"
        }
    }
}
```

## Required Fields

- `formatVersion`: currently `1`.
- `startupRenderer`: `OpenGL` or `Vulkan`.
- `tileWidth` and `tileHeight`: positive integer tile dimensions.
- `tilesets`: at least one existing image path.
- `playerCharacters`: at least one known `CharacterType`.
- `Walking` and `Running` sprites for each configured player character.

## Optional Fields

- `defaultMap`: authored/persisted map loaded by Continue, used as the New Game
  baseline, and written by the editor. Missing files are allowed; Rift generates
  a default map using `defaultMapSize`.
- `defaultMapSize`: used only when the default map cannot be loaded.
- `npcSprites`: used by editor NPC placement and map-load type lookup.
- `fonts`: project fonts tried before renderer system fallbacks.
- `Bicycle`: optional player sprite; missing files warn but do not block startup.

## Troubleshooting

Common diagnostics:

| Message | Meaning |
|---------|---------|
| `Missing tileset asset` | A required tileset path does not exist relative to the manifest. |
| `Unknown CharacterType name` | The player character key does not match `CharacterType` names such as `BW1_MALE`. |
| `Default map was not found` | Startup will generate a map and editor saves will write to the configured path. |
| `No project font candidates configured` | Text rendering falls back to renderer defaults and system fonts. |

Use forward slashes in JSON paths. Absolute paths work, but relative project
paths are easier to share.
