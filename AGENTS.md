# Repository Guidelines

## Rules

Ask when unclear. If intent, architecture, or requirements are ambiguous, ask before coding.

Flag uncertainty. If an approach, dependency, or technical detail is uncertain, say so before proceeding.

Challenge bad direction. If my request conflicts with settled practice or likely long-term maintainability, point it out and suggest a better path.

End with omissions. After each task, state what you changed and what you intentionally did not do.

## Documentation

Document the code using ASD-STE100-inspired Simplified Technical English: use short, direct sentences, one term per concept, active voice, explicit conditions, and avoid idioms, unnecessary synonyms, or ambiguous wording. Focus documentation on intent, constraints, side effects, and non-obvious behavior;

## Project Structure & Module Organization

Rift is a C++23 RPG engine. Production code lives in the flat `src/` directory, with related files grouped by subsystem and named in PascalCase (for example, `WeatherDirector.cpp` and `.hpp`). Google Test coverage lives in `tests/`, generally mirroring the production type as `FeatureTests.cpp`. Runtime GLSL is under `shaders/`; game content is under `assets/` and is registered through `rift.project.json`. Architecture and feature notes live in `docs/`. Treat `external/` as vendored code and `build/`, `build-cdb/`, `site/`, and generated `.spv` files as build output.

## Build, Test, and Development Commands

Use Windows, Visual Studio 2022, vcpkg, and the Vulkan SDK. Set `VCPKG_ROOT` before configuring.

- `.\build.bat` formats C++ sources, configures CMake, runs `clang-tidy`, builds Debug and Release, and optionally generates Doxygen output. Pass `--skip-tidy` only for a deliberate quick build.
- `cmake --preset default` configures the shared `build/` tree.
- `cmake --build --preset default-debug` or `default-release` builds one configuration.
- `cmake --build --preset tests-release` followed by `ctest --preset tests-release` builds and runs tests with failures displayed.
- `.\test.bat` wraps the Release test workflow; `.\run.bat` launches `build\Release\rift.exe` from the repository root.

## Coding Style & Naming Conventions

Run `clang-format -i` on changed `.cpp`/`.hpp` files. The repository style uses four spaces, no tabs, Allman braces, left-aligned pointers/references, and a 100-column limit. Use PascalCase for files, types, functions, methods, namespaces, and enum values; camelCase for locals, parameters, and plain-struct fields; `m_PascalCase` for class members; and `UPPER_SNAKE_CASE` for macros/constants. Headers use Doxygen comments; implementation files use plain `//` comments. Follow `.clang-tidy` and the fuller rules in `CONTRIBUTING.md`.

## Testing Guidelines

Tests use Google Test with CTest discovery. Name files `FeatureTests.cpp` and cases descriptively, such as `TEST_F(TimeManagerTest, GetTimePeriod_Dawn)`. Add tests for non-trivial behavior, especially parsing, math, serialization, state machines, public APIs, and bug regressions. No numeric coverage threshold is documented, but all tests must pass.

## Commit & Pull Request Guidelines

Recent history uses a gitmoji followed by a concise imperative subject, for example `🐛 Keep Vulkan particles self-lit`. Keep each commit and PR focused. PR descriptions should state what changed, why, tradeoffs, and validation performed; link relevant issues and include screenshots for rendering or editor changes. Avoid mixing feature work with unrelated refactors or formatting churn.
