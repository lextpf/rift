# Using the Level Editor

The Rift Engine ships a built-in level editor that mutates the active `Tilemap` in real time. Open it with `E` while in-game; the tile picker opens automatically and editor input takes over from gameplay (player movement, dialogue, etc. are suppressed while the editor is active).

## Mode Table

Only one sub-mode is active at a time. Each mode is selected by hotkey:

| Key | Mode               | Left-Click Action                | Right-Click Action           |
|-----|--------------------|----------------------------------|------------------------------|
|   T | Tile Picker        | Select tile / multi-tile region  | -                            |
|   M | Navigation Edit    | -                                | Toggle walkability (drag)    |
|   N | NPC Placement      | Place / remove NPC               | -                            |
|   B | No-Projection Edit | Set no-projection flag (flood)   | Clear flag (flood)           |
|   G | Structure Edit     | Anchor + flood assign structure  | Clear structure assignment   |
|   H | Elevation Edit     | Paint elevation value            | Clear elevation              |
|   J | Particle Zone Edit | Drag to create zone              | Remove zone                  |
|   K | Animation Edit     | Apply animation to tile          | Remove animation             |
|   Y | Y-Sort-Plus Edit   | Set Y-sort-plus flag             | Clear Y-sort-plus flag       |
|   O | Y-Sort-Minus Edit  | Set Y-sort-minus flag            | Clear Y-sort-minus flag      |
|   - | Default            | Place selected tile (drag)       | Toggle collision (drag)      |

Mode-specific notes:

- **Tile Picker (T)** — Drag to select a multi-tile region; placement stamps the whole region per click.
- **Navigation (M)** — Drag-paint walkability. Clearing a tile that an NPC is standing on removes that NPC; the snapshot is captured by the undo stack so `Ctrl+Z` brings the NPC back.
- **NPC Placement (N)** — Single-click toggles. Placement only succeeds on walkable (navmesh) tiles. The NPC's dialogue tree is randomly assigned at placement time.
- **No-Projection (B)** — Single click sets the flag on the current layer. Shift+click flood-fills the connected component. Right-click clears the flag on **all 10 layers** for the clicked tile (or flood across one layer).
- **Structure (G)** — Two-step workflow: Ctrl+click for left anchor, Ctrl+click again for right anchor (creates the structure). Then Shift+click flood-fills the connected component, stamping `noProjection` and assigning the new structure id. Right-click clears the structure assignment.
- **Elevation (H)** — Drag-paints the current elevation value. Right-click clears to 0.
- **Particle Zone (J)** — Drag-release defines a rectangular zone. Right-click removes the zone under the cursor.
- **Animation (K)** — In the tile picker, click to add frames to the active sequence; press Enter to finalize the animation definition. Then click on map tiles to apply. Right-click removes the animation from a tile.
- **Y-Sort-Plus (Y) / Y-Sort-Minus (O)** — Per-layer flags. Single click for one tile, Shift+click for flood-fill, right-click to clear.
- **Default** — Tile placement (left-click drag) and collision toggle (right-click drag).

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
- Elevation (paint, right-click clear)
- Navigation (drag-paint, with displaced-NPC restore)
- NPC placement / removal
- No-projection / Y-sort-plus / Y-sort-minus flags (single, flood-fill, multi-layer right-click clear)
- Structure: add (anchor placement), remove (right-click clear, captures per-tile reference snapshot)
- Particle zones: add (drag-release), remove (right-click)
- Animation definitions (Enter on collected frames) and per-tile animation apply / remove
- Region paste (Ctrl+V — see below)

What's *not* tracked:

- Mid-drag mode switches strand the in-progress drag without committing. Tiles already painted during the dropped drag stay (no crash, but no undo entry for them). Press the desired mode key cleanly between drags.
- Loading a map (`save.json`) clears the undo stack — captured commands cannot be safely Reverted against a different map.

## Region Copy-Paste

| Key            | Action |
|----------------|--------|
| `Ctrl+drag`    | Define a rectangular tile-region selection on the map. Works in any mode except Structure (which uses Ctrl-click for anchor placement). Press `Esc` to clear an active selection. |
| `Ctrl+C`       | Copy the selected region into the clipboard. Captures all 10 layers (tile id, rotation, no-projection, structure id, y-sort flags, animation id) plus collision, navigation, and elevation per tile. |
| `Ctrl+V`       | Paste the clipboard at the cursor (top-left of paste). Out-of-bounds tiles are skipped; the paste is undoable as a single command. |

NPCs, particle zones, and structures are not included in the clipboard. NPC copy would require texture re-load and dialogue-tree replication; structures and particle zones use vector-index identity that doesn't transfer cleanly across maps. These are deferred follow-ups; for now, copy-paste covers tile data and per-tile flags only.

## Saving and Loading

| Key   | Action |
|-------|--------|
| `S`   | Save the current map to `save.json` in the working directory. |
| `L`   | Reload `save.json`. Discards the undo stack and current selection. |

The save file is human-readable JSON; manual edits work but the editor is the supported entry point.

## Other Hotkeys

| Key            | Action |
|----------------|--------|
| `1`–`0`        | Switch to layer 1–10 for tile placement, no-projection, y-sort, etc. |
| `R`            | Rotate the selected tile (or multi-tile selection) by 90°. |
| Arrow keys     | Pan the camera (or the tile picker when it's open). |
| `Shift+arrows` | Fast-pan (2.5× speed). |
| `Esc`          | Cancel the current operation (anchor placement, selection, animation frames). |
| `Del` (drag)   | Delete tiles under the cursor on the current layer. |
| `Ctrl+scroll`  | Zoom (camera, or tile picker when it's open). |

## Architecture Notes

`Editor` is decoupled from `Game` via `EditorContext` (`src/Editor.h:45-65`), a struct of references built fresh every frame in `Game::MakeEditorContext()`. Editor never includes `Game.h` and never stores the context — reference members dangle if held across frames. This is why `EditorCommand` subclasses capture concrete tile coordinates / IDs / values rather than pointers into the context.

`UndoRedoStack` (`src/UndoRedoStack.h`) holds two deques of `std::unique_ptr<EditorCommand>`. New commands push to the undo stack and clear the redo stack; capacity overflow drops the oldest entry from the front. Stroke accumulators in `src/EditorStrokeAccumulators.h` batch per-frame mutations during a drag-paint into a single composite command at mouse-up.
