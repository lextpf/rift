# Using the Level Editor

Rift includes a built-in level editor that mutates the active `Tilemap` in real time. Open the developer console with `F12` and run `ed` (alias of `editor`); the tile picker opens automatically and editor input takes over from gameplay (player movement, dialogue, etc. are suppressed while the editor is active).

## Mode Table

Only one sub-mode is active at a time. Each mode is selected by hotkey:

| Key | Mode               | Left-Click Action                | Right-Click Action           |
|-----|--------------------|----------------------------------|------------------------------|
|   T | Tile Picker        | Select tile / multi-tile region  | -                            |
|   M | Navigation Edit    | -                                | Toggle walkability (drag)    |
|   N | NPC Placement      | Place / remove NPC               | -                            |
|   B | Stance Edit        | Paint selected stance (flood)    | Reset to Flat (flood)        |
|   G | Structure Edit     | Anchor + flood assign structure  | Clear structure assignment   |
|   H | Elevation Edit     | Paint height + role              | Clear height + role          |
|   J | Particle Zone Edit | Drag to create zone              | Remove zone                  |
|   K | Animation Edit     | Apply animation to tile          | Remove animation             |
|   Y | Y-Sort-Plus Edit   | Set Y-sort-plus flag             | Clear Y-sort-plus flag       |
|   O | Y-Sort-Minus Edit  | Set Y-sort-minus flag            | Clear Y-sort-minus flag      |
|   - | Default            | Place selected tile (drag)       | Toggle collision (drag)      |

Every mode key except `T` is a toggle: pressing the active mode's own key again returns to Default.
Each mode change first calls `ClearAllEditModes()`, which discards the previous mode's transient
state - structure anchors and anchor step, collected animation frames, and the flood flag. `T` is
different: it only shows or hides the tile picker overlay and leaves the current sub-mode alone.

Mode-specific notes:

- **Tile Picker (T)** - Drag to select a multi-tile region; placement stamps the whole region per click.
- **Navigation (M)** - Drag-paint walkability. Clearing a tile that an NPC is standing on removes that NPC; the snapshot is captured by the undo stack so `Ctrl+Z` brings the NPC back.
- **NPC Placement (N)** - Single-click toggles. Placement only succeeds on walkable (navmesh) tiles. The NPC's dialogue tree is randomly assigned at placement time.
- **Stance (B)** - Paints the per-tile `TileStance` that decides whether a tile's artwork lies on the ground or stands up, and if it stands, whether it turns toward the camera. `,` / `.` cycle the stance being painted (`Prop` -> `Wall` -> `Structure`); `Flat` is not in the cycle because right-click is how a cell is cleared. Single click paints one tile, Shift+click flood-fills the connected component, right-click resets to `Flat`. **Both buttons act on the current layer only.**
  - `Flat` - ground artwork: grass, paths, water, decals. Lies on the ground plane and draws depth-free, so it never occludes an actor.
  - `Prop` - a pole: lantern, bush, signpost. One tile tall on its own row, always turns to face the camera, never welds to a neighbour.
  - `Wall` - a surface: fence panel, hedge, low retaining wall. One tile tall on its own row, locked to the grid. A north-south run of these stays parallel instead of fanning open like venetian blinds.
  - `Structure` - one tile of a multi-tile-tall body: house facade, bridge railing, cliff face. Anchored on the run's base row and lifted; a one-tile-wide body still turns, a wider one is grid-locked to hold its footprint.
- **Structure (G)** - Two-step workflow: Ctrl+click for left anchor, Ctrl+click again for right anchor (creates the structure). Then Shift+click flood-fills the connected component, stamping `TileStance::Structure` and assigning the new structure id. Right-click clears the structure assignment.
- **Elevation (H)** - Paints two things that belong together: the per-**cell** height in pixels (scroll to change, steps of 2, clamped to [-32, +32]) and the per-**layer** `ElevationRole` that decides whether *this* layer's artwork rises to it. `,` / `.` cycle the role; `Ground` is not in the cycle because right-click is the clear action. Both writes commit as one undo entry.
  - `Ground` - artwork stays on the ground plane whatever the cell's height says. This is what keeps water painted under a bridge from rising with the deck.
  - `Raised` - artwork sits flat at the cell's height (deck, plateau).
  - `Ramp` - artwork slopes, its edges derived from the same layer's neighbours along the cell's elevation axis. A ramp meets a `Raised` deck at the deck's exact height and bare ground at 0.

  Height alone changes nothing visually - it drives collision and walkability as it always has. Only a role makes a layer rise. Note `MAX_STEP_HEIGHT` is 8: if you raise a deck, keep consecutive ramp steps within 8 or the player cannot climb it.
- **Particle Zone (J)** - Drag-release defines a rectangular zone. Right-click removes the zone under the cursor.
- **Animation (K)** - In the tile picker, click to add frames to the active sequence; press Enter to finalize the animation definition. Then click on map tiles to apply. Right-click removes the animation from a tile.
- **Y-Sort-Plus (Y) / Y-Sort-Minus (O)** - Per-layer flags. Single click for one tile, Shift+click for flood-fill, right-click to clear. These affect **draw order only** and have no bearing on whether a tile stands up - that is the `B` mode stance. (An earlier 3D rule did infer uprightness from them, which stood flat decals on their edge; see `src/TileStance.hpp`.)
- **Default** - Tile placement (left-click drag) and collision toggle (right-click drag).

## Persistent HUD

When the editor is active, a bottom HUD shows the current tool, layer, selected tile, rotation, elevation, selected NPC/particle/structure, cursor tile, active selection size, saved/unsaved state, and valid left/right click actions. Tile names are generated from the tile ID and atlas coordinate because the project does not currently define tile-name metadata.

The unsaved indicator means the map has changed since the last successful save or load. It does not perform a full content-hash comparison.

## Undo / Redo

Every editor mutation is wrapped in an `EditorCommand` and pushed onto a bounded undo stack (capacity 100, FIFO eviction). Hotkeys:

| Key       | Action |
|-----------|--------|
| `Ctrl+Z`  | Undo the most recent action. |
| `Ctrl+Y`  | Redo the most recently undone action. New mutations clear the redo stack. |

A status toast at the bottom of the screen shows the label of the action that was undone/redone (e.g., `Undo: Place 12 tile(s)`).

What's tracked:

- Tile placement (single, drag-paint, multi-tile region)
- Collision toggle (single, drag-paint)
- Elevation (paint, right-click clear) - `ElevationSetCmd` for height, `SetElevationRolesCmd` for the per-layer role, bundled as one `CompositeCmd` when a click changes both
- Navigation (drag-paint, with displaced-NPC restore)
- NPC placement / removal
- No-projection / Y-sort-plus / Y-sort-minus flags (single, flood-fill, multi-layer right-click clear)
- Structure: add (anchor placement), remove (right-click clear, captures per-tile reference snapshot)
- Particle zones: add (drag-release), remove (right-click)
- Animation definitions (Enter on collected frames) and per-tile animation apply / remove
- Region paste (Ctrl+V - see below)

What's *not* tracked:

- Mid-drag mode switches strand the in-progress drag without committing. Tiles already painted during the dropped drag stay (no crash, but no undo entry for them). Press the desired mode key cleanly between drags.
- Loading a map (`rift.save.json`) clears the undo stack - captured commands cannot be safely Reverted against a different map.

## Region Copy-Paste

| Key            | Action |
|----------------|--------|
| `Ctrl+drag`    | Define a rectangular tile-region selection on the map. Works in any mode except Structure (which uses Ctrl-click for anchor placement). Press `Esc` to clear an active selection. |
| `Ctrl+C`       | Copy the selected region into the clipboard. Captures 10 layers (tile id, rotation, `flipX`, `flipY`, stance, structure id, y-sort flags, animation id, elevation role) plus collision, navigation, and elevation per tile. |
| `Ctrl+V`       | Paste the clipboard at the cursor (top-left of paste). Out-of-bounds tiles are skipped; the paste is undoable as a single command. |

The clipboard holds exactly 10 layers (`ClipboardCell::LAYER_COUNT`), while the tilemap layer stack
is dynamic. On a map with more layers, layers 10 and above are neither copied from the source nor
overwritten at the destination.

NPCs, particle zones, and structures are not included in the clipboard. NPC copy would require texture re-load and dialogue-tree replication; structures and particle zones use vector-index identity that doesn't transfer cleanly across maps. These are deferred follow-ups; for now, copy-paste covers tile data and per-tile flags only.

## Saving and Loading

| Key   | Action |
|-------|--------|
| `S`   | Save the current map to the `defaultMap` path from `rift.project.json`. |
| `L`   | Reload the configured `defaultMap`. Discards the undo stack and current selection. |

The save file is human-readable JSON; manual edits work but the editor is the supported entry point.
If no manifest is found, the default path remains `rift.save.json`.

## Other Hotkeys

| Key            | Action |
|----------------|--------|
| `1`-`0`        | Switch to layer 1-10 for tile placement, stance, y-sort, etc. |
| `R`            | Requires the tile picker closed (`T`), which the editor opens on activation. Rotate the brush by 90 degrees (affects the next stamp, 1x1 included) AND rotate the tile under the cursor on the current layer. Same dual contract as `F`. The tile edit goes through `PlaceTilesCmd` so `Ctrl+Z` reverts it; an empty cell is left alone rather than reported as rotated. |
| `F`            | Requires the tile picker closed (`T`). Toggle brush `flipX` (the upcoming stamp paints mirrored around the vertical axis) AND reflect the active selection along X. Selection target is the Ctrl+drag rectangle if active, else the tile under the cursor on the current layer (toggles per-tile `flipX` and negates rotation). Reserved for `noProjection` toggle while in particle-zone mode (`J`). |
| `Shift+F`      | Toggle brush `flipY` AND reflect the active selection along Y. Same gate and selection contract as `F`; toggles per-tile `flipY` and negates rotation. |
| Arrow keys     | Pan the camera (or the tile picker when it's open). |
| `Shift+arrows` | Fast-pan (2.5x speed). |
| `Esc`          | Cancel the current operation (anchor placement, selection, animation frames). |
| `Del` (drag)   | Delete tiles under the cursor on the current layer. |
| `Ctrl+scroll`  | Zoom (camera, or tile picker when it's open). |

Reflection caveat: when reflecting a region that overlaps a no-projection structure, individual tile content is flipped but the structure's `leftAnchor`/`rightAnchor` (world coords on `NoProjectionStructure`) stay put. This matches `R`-rotate semantics - reposition the anchors manually if needed.

## Architecture Notes

`Editor` is decoupled from `Game` via `EditorContext` (see `struct EditorContext` in `src/Editor.hpp`), a struct of references built fresh every frame in `Game::MakeEditorContext()`. Editor never includes `Game.hpp` and never stores the context - reference members dangle if held across frames. This is why `EditorCommand` subclasses capture concrete tile coordinates / IDs / values rather than pointers into the context.

`UndoRedoStack` (`src/UndoRedoStack.hpp`) holds two deques of `std::unique_ptr<EditorCommand>`. New commands push to the undo stack and clear the redo stack; capacity overflow drops the oldest entry from the front. Stroke accumulators in `src/EditorStrokeAccumulators.hpp` batch per-frame mutations during a drag-paint into a single composite command at mouse-up.

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Input as "Mouse Drag"
    participant Accum as "Stroke Accumulator"
    participant Cmd as "Composite Command"
    participant Stack as "UndoRedoStack"
    participant Tilemap

    Input->>Accum: Touch tile/value changes
    Input->>Tilemap: Apply live preview edits
    Input->>Accum: Mouse released
    Accum->>Cmd: Build command from captured before/after state
    Cmd->>Stack: Push undo, clear redo
    Stack->>Cmd: Undo()
    Cmd->>Tilemap: Restore before state
    Stack->>Cmd: Redo()
    Cmd->>Tilemap: Restore after state
</pre>
\endhtmlonly
