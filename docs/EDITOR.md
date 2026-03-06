# Using the Level Editor

The Rift Engine features a built-in level editor that allows you to modify maps in real-time.

## Entering Editor Mode

Press `E` while in-game to toggle Editor Mode.
When active, you will see editor UI overlays (if enabled) and have access to editor tools.

## Creating a Map

### 1. Placing Tiles
1.  Press `T` to open the Tile Picker.
2.  Use the mouse wheel to scroll through available tiles.
3.  Click a tile to select it.
4.  Press `1`, `2`, `3`, or `4` to select the target layer:
    *   **Layer 1**: Background (Ground)
    *   **Layer 2**: Decoration (Below Player)
    *   **Layer 3**: Overhead (Above Player)
    *   **Layer 4**: Top (Always on top)
5.  Click on the map to place the tile. You can click and drag to paint.
6.  Right-click on a tile in the map to delete it (or replace with empty).

### 2. Adding Collisions
1.  Ensure you are in the default editor mode (close Tile Picker with `T` if open).
2.  Right-click on a tile to toggle its **Collision** status.
    *   **Red overlay**: Collision enabled (Player cannot walk here).
    *   **No overlay**: Walkable.

### 3. Creating Navigation Mesh (for NPCs)
1.  Press `M` to enter Navmesh Mode.
2.  Right-click on a tile to toggle its **Navmesh** status.
    *   **Cyan overlay**: Navmesh enabled (NPCs can walk here).
3.  NPCs will wander randomly within connected navmesh tiles.

### 4. Placing NPCs
1.  Press `N` to enter NPC Mode.
2.  Left-click on a tile to place an NPC.
    *   *Note*: NPCs require a valid navmesh to move.
3.  Left-click an existing NPC to remove it.

## Saving Changes
Press `S` to save your changes. The map is saved to `map.json` in the executable's directory.

## Map Format
The map is stored in a JSON format (`map.json`). It contains:
*   Map dimensions
*   Tile data for all layers
*   Collision data
*   Navmesh data
*   NPC positions and types

This file is human-readable and can be manually edited if necessary, though using the in-game editor is recommended.

