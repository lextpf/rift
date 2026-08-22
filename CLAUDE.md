# CLAUDE.md

## Rules

Ask when unclear. If intent, architecture, or requirements are ambiguous, ask before coding.

Flag uncertainty. If an approach, dependency, or technical detail is uncertain, say so before proceeding.

Challenge bad direction. If my request conflicts with settled practice or likely long-term maintainability, point it out and suggest a better path.

End with omissions. After each task, state what you changed and what you intentionally did not do.

## Documentation

Document the code using ASD-STE100-inspired Simplified Technical English: use short, direct sentences, one term per concept, active voice, explicit conditions, and avoid idioms, unnecessary synonyms, or ambiguous wording. Focus documentation on intent, constraints, side effects, and non-obvious behavior;

## Project

Rift is a 2.5D tile-based RPG engine in C++23, Windows/MSVC only, with runtime-switchable
OpenGL 4.6 and Vulkan 1.4 backends, an in-game level editor, and a developer console that is the
primary interaction surface. `src/` is flat (~165 files) and grouped by naming convention, not
by directory.

## Commands

### Build

```
build.bat                  # full gate: clang-format src/ -> cmake configure -> clang-tidy (blocking) -> Debug + Release -> doxygen
build.bat --skip-tidy      # same without the slow, blocking static-analysis step
cmake --preset default     # configure only (VS 2022 generator, vcpkg manifest, build/)
cmake --build build --config Release --target rift
```

### Test

```
test.bat                                                  # configure + build rift_tests + run everything (pauses at the end)
cmake --build build --config Release --target rift_tests
build\Release\rift_tests.exe --gtest_filter=WeatherBlendTests.*   # a single suite / test
build\Release\rift_tests.exe --gtest_list_tests
ctest --test-dir build -C Release -R Fog --output-on-failure     # CI runs plain `ctest -C Release`
```

### Run

```
run.bat                    # runs build\Release\rift.exe from the repo root (correct CWD for assets/, shaders/, rift.project.json)
```

`rift.exe` takes no arguments; everything is a console command. Crashes and startup failures are
appended to `rift.project.log`.

### Static analysis

- The real lint gate is **build.bat step 3**: `clang-tidy --quiet -p build-cdb` over every
  `src/*.cpp` and `tests/*.cpp`, one file at a time. It stops the pipeline on any diagnostic.
- **Do not use `cmake --build build --target tidy`.** That standalone target is misconfigured
  (it misses the C++23 / glm flags) and emits phantom errors.
- Two build trees, on purpose: `build/` is the real VS 2022 / MSBuild tree holding both `rift` and
  `rift_tests`; `build-cdb/` is a Ninja sidecar that is **only ever configured, never built**, and
  exists solely to emit a real `compile_commands.json` for clang-tidy and clangd. build.bat
  regenerates it when `CMakeLists.txt` is newer; after changing include paths or targets, refresh
  it with `cmake --preset compile-db`.
- CI enforces clang-format over `src/` only. `tests/` is never formatted by CI or by build.bat
  step 1 (clang-tidy does still lint it).

## Architecture

### ECS core

The engine uses a custom single-header ECS at `external/ecs/ecs.hpp` (lextpf/ecs), included as
`<ecs.hpp>`: plain-struct components, view queries, no RTTI, no exceptions.

- `Game` (`src/Game.hpp`) is the composition root. It owns the window, renderer, tilemap, the
  registry `m_World`, and every subsystem **by value**, and drives Initialize -> Run -> Shutdown.
  The implementation is split across partials by concern: `Game.cpp` (lifecycle + frame loop),
  `GameInput.cpp`, `GameMenus.cpp` (title/pause menus plus the title and 3D render paths),
  `GameDialogue.cpp`.
- **Shared services are not components.** Game publishes non-owning pointers into
  `m_World.globals()` as a `WorldServices` bundle (`textures`, `dialogue`, `assets`, `npcRng`,
  `gameState`). That is how stateless systems reach shared state. Every pointer is nullable —
  tests routinely publish a partial bundle, so null-check rather than assume a wired world.
- Entities are granular components plus **stateless free-function systems**: `MotionSystem`,
  `PlayerMovementSystem`, `PlayerSystem`, `NpcAiSystem`, `CollisionSystem`, `SurfaceSystem`,
  `CharacterKinematics`, `EntityStore` (spawn / despawn / query), and the `*Render` draw helpers.
- There are no `PlayerCharacter`, `NonPlayerCharacter`, or `CollisionResolver` classes.
  `docs/ARCHITECTURE.md` still describes them; the ECS migration dissolved them. **Trust the code
  over that document** wherever the two disagree.

### Rendering

`IRenderer` is a strategy interface; `RendererFactory` picks the backend at runtime. Both backends
are always compiled in — Vulkan is a hard `find_package(Vulkan REQUIRED)`, so never introduce
`#ifdef USE_VULKAN` guards.

- Adding or changing an `IRenderer` method means editing `IRenderer.hpp` **and**
  `RendererMacros.hpp` together. `RIFT_DECLARE_COMMON_RENDERER_METHODS` forces byte-identical
  override sets in both backends; bypassing it for a one-off override defeats the point.
- `renderer.set` destroys and recreates the window and renderer, then `TextureStore` re-uploads
  every texture. Anything that caches an `IRenderer*` across that switch dangles.
- `shaders/*.vert|frag` are GLSL 450 loaded at runtime by the OpenGL backend; CMake compiles them
  to `*.spv` via `glslangValidator` for Vulkan. `*.spv` is gitignored deliberately (build
  artifact, not a committed fallback). A stale `.spv` silently keeps an old Vulkan shader alive —
  delete it when Vulkan output disagrees with the GLSL source.
- Two render paths coexist: the default flat 2.5D path, and an in-progress world-space 3D orbit
  camera behind the `world3d` console toggle (`CameraRig`, `Billboard`, `Frustum`,
  `Geometry3D.vert/frag`).

### World data

`Tilemap` owns a dynamic stack of tile layers (currently 10) plus derived per-cell grids:
`CollisionMap` (player blocking), `NavigationMap` (NPC walkability), elevation, y-sort flags and
upright/structure flags, all backed by `BoolGrid`. Use `GetLayerCount()`, never a hard-coded 10.
Maps serialize sparsely to JSON. Edits route through `NavigationRecalc` to rebuild navigation and
patrol routes.

### Developer console

F12 opens it; `help` lists the ~100 registered commands (`src/ConsoleCommands.cpp`). Each command
is a free function over a `CommandContext` — a per-invocation bundle of nullable pointers into
Game state — which is what makes commands unit-testable without a `Game` (see
`tests/ConsoleCommandsTests.cpp`). Never store a `CommandContext`, or any pointer taken out of
one, past the handler that received it.

**README.md's keybinding tables are stale.** `E`, `F1`, `F2`, `F3` and `F5` are no longer bound.
Gameplay input is WASD + Shift; everything else is a console command — `ed` (editor toggle),
`renderer.set opengl|vulkan`, `debug.overlays`, `time.set`, `world3d`, `weather.next`.

### Editor

`Editor.hpp` with partials `EditorInput.cpp` / `EditorRendering.cpp`. Every mutation goes through
an `EditorCommand` on the `UndoRedoStack`; drag-painting batches through
`EditorStrokeAccumulators`. Add new edits as commands, not as direct tilemap writes.

## Non-obvious wiring when adding code

- **New test file:** `tests/*.cpp` is globbed, so the file itself is picked up automatically. But
  the `src/*.cpp` translation units linked into the test binary are an explicit `TEST_LIB_SOURCES`
  list in `CMakeLists.txt`. If a test pulls in a src file that is not on that list, add it — and
  only if it is renderer-free. The test binary links GL/VK but never creates a context, so tests
  must not call render paths.
- Symbols that are linked-but-never-called (to satisfy the linker) belong in `tests/TestStubs.cpp`.
- **New enum:** specialize `EnumTraits` with `Count` and `Names[]`. Some tests hard-code `Count` as
  a layout invariant (e.g. `ParticleType.EnumLayoutInvariant`).
- **New `ParticleType`** additionally needs a `kParticleVisuals` row in `ParticleSystem.cpp` (order
  must match the enum) and a case in `GetParticleTypeColor` in `EditorRendering.cpp` — without the
  latter it renders white in the editor.
- Version numbers live only in `src/Version.hpp`. CMake regex-parses those four `#define` lines, so
  keep each on one line in exactly that form.
- `assets/` is gitignored and not in the repository. `rift.project.json` wires tilesets, sprites,
  fonts, particles, tile size and the startup renderer — configure assets there, never by editing
  `Game.cpp`.
- The game boots to a title screen; the world is not loaded until New Game / Continue, which
  matters when smoke-testing gameplay changes.

## Style

`CONTRIBUTING.md` has the full rules. The parts that are easy to get wrong:

- clang-format (Google base, Allman braces, 100 columns, 4 spaces, left-aligned pointers) owns all
  layout — run it instead of hand-tuning. It does **not** reflow comment text, so keep comment
  bodies inside 100 columns yourself.
- Naming: PascalCase for files, types, functions and namespaces; camelCase for locals and
  parameters; `m_PascalCase` for class members; unprefixed camelCase for plain-struct fields
  (never `m_`); UPPER_SNAKE_CASE for macros and constants.
- **Doc comments are split by file kind, and de-syncing them is the most common mistake:**
  - `.hpp` — Doxygen. A doc comment of 1-2 lines uses `///`; 3 or more lines becomes a `/** */`
    block with `@brief`. This threshold applies to *every* entity, fields included. Use trailing
    `///<` for members whose description fits on the line. Type/file blocks order tags as kind tag
    (`@class`/`@struct`/`@enum`), `@brief`, `@author`, `@ingroup`. `@addtogroup` exists only in
    `DoxygenGroups.hpp`; everywhere else references groups with `@ingroup`. Never `@file`,
    `@returns`, `@defgroup`, or the `//!` / `/*! */` bang variants.
  - `.cpp` — plain `//` line comments only. No `///`, no `/** */`, no Doxygen commands at all,
    with the single exception of an `// @author Name (url)` line.
- Keep ASCII diagrams and worked example traces in algorithm-heavy comments; they are house style,
  not clutter.
- Braces on every control-flow body, early returns over nesting, `enum class` over unscoped enums,
  ownership expressed in the type (`unique_ptr` owns, raw pointers/references do not), no
  `using namespace` in headers.
