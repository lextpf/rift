<div align="center">

# rift
**A modern 2.5D game engine built with C++23.**

🏠 [Mainpage](./docs/MAINPAGE.md) | 🧱 [Building](./docs/BUILDING.md) | 🏛️ [Architecture](./docs/ARCHITECTURE.md) | 🤝 [Contributing](./CONTRIBUTING.md)

![Engine](https://img.shields.io/badge/2.5D-Engine-1e293b.svg?style=flat&logo=data:image/svg+xml;base64,PHN2ZyBoZWlnaHQ9IjIwMHB4IiB3aWR0aD0iMjAwcHgiIHZlcnNpb249IjEuMSIgaWQ9Il94MzJfIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHhtbG5zOnhsaW5rPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3hsaW5rIiB2aWV3Qm94PSIwIDAgNTEyIDUxMiIgeG1sOnNwYWNlPSJwcmVzZXJ2ZSIgZmlsbD0iI2ZmZmZmZiIgc3Ryb2tlPSIjZmZmZmZmIj48ZyBpZD0iU1ZHUmVwb19iZ0NhcnJpZXIiIHN0cm9rZS13aWR0aD0iMCI+PC9nPjxnIGlkPSJTVkdSZXBvX3RyYWNlckNhcnJpZXIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PC9nPjxnIGlkPSJTVkdSZXBvX2ljb25DYXJyaWVyIj4gPHN0eWxlIHR5cGU9InRleHQvY3NzIj4gLnN0MHtmaWxsOiNmZmZmZmY7fSA8L3N0eWxlPiA8Zz4gPHBhdGggY2xhc3M9InN0MCIgZD0iTTUxMC40NDEsMzIyLjg5NGwtMjkuNzYtMTI2LjU2Yy0xNS4wNzMtNTYuMjUyLTY2LjA0Ny05NS4zNjMtMTI0LjI4Ni05NS4zNjNIMTU1LjYwNCBjLTU4LjIzOSwwLTEwOS4yMTIsMzkuMTEtMTI0LjI4NSw5NS4zNjNsLTI5Ljc2LDEyNi41NmMtNy42MTgsMzYuMzAzLDEzLjM0LDcyLjUyNCw0OC42MjYsODQuMDAxbDEuNzQsMC41NTggYzMyLjY1OCwxMC42MzIsNjguMjg4LTMuMDcsODUuNDI5LTMyLjgzMWwyNS44OTQtMzguMjg5YzYuNTAyLTExLjI4OCwxOC41MzgtMTguMjUsMzEuNTUtMTguMjVoMTIyLjQwNiBjMTMuMDEyLDAsMjUuMDQ4LDYuOTYyLDMxLjU0OSwxOC4yNWwyNS44OTQsMzguMjg5YzE3LjE0MiwyOS43NjEsNTIuNzc5LDQzLjQ2Myw4NS40MzgsMzIuODMxbDEuNzMyLTAuNTU4IEM0OTcuMSwzOTUuNDE4LDUxOC4wNiwzNTkuMTk3LDUxMC40NDEsMzIyLjg5NHogTTE5MS4wNDYsMjM4LjA4MWgtNDEuNjg5djQxLjY5NmgtMzYuMjk1di00MS42OTZINzEuMzczdi0zNi4yNzloNDEuNjg5di00MS42OCBoMzYuMjk1djQxLjY4aDQxLjY4OVYyMzguMDgxeiBNMzc0LjcyOCwxNTEuNDM2YzEyLjYyNiwwLDIyLjg0NywxMC4yMjEsMjIuODQ3LDIyLjg0OGMwLDEyLjYxLTEwLjIyMSwyMi44MzEtMjIuODQ3LDIyLjgzMSBjLTEyLjYxLDAtMjIuODMxLTEwLjIyMS0yMi44MzEtMjIuODMxQzM1MS44OTcsMTYxLjY1NiwzNjIuMTE4LDE1MS40MzYsMzc0LjcyOCwxNTEuNDM2eiBNMzI5LjA0OSwyNDIuODAxIGMtMTIuNjEsMC0yMi44MzktMTAuMjMtMjIuODM5LTIyLjg1NmMwLTEyLjYwMiwxMC4yMjktMjIuODMxLDIyLjgzOS0yMi44MzFjMTIuNjE4LDAsMjIuODM5LDEwLjIyOSwyMi44MzksMjIuODMxIEMzNTEuODg5LDIzMi41NzIsMzQxLjY2OCwyNDIuODAxLDMyOS4wNDksMjQyLjgwMXogTTM3NC43MjgsMjg4LjQ3MWMtMTIuNjEsMC0yMi44MzEtMTAuMjIxLTIyLjgzMS0yMi44MzEgYzAtMTIuNjI3LDEwLjIyMS0yMi44NDgsMjIuODMxLTIyLjg0OGMxMi42MjYsMCwyMi44NDcsMTAuMjIxLDIyLjg0NywyMi44NDhDMzk3LjU3NSwyNzguMjUsMzg3LjM1NCwyODguNDcxLDM3NC43MjgsMjg4LjQ3MXogTTQyMC40MDYsMjQyLjgwMWMtMTIuNjEsMC0yMi44MzItMTAuMjMtMjIuODMyLTIyLjg1NmMwLTEyLjYwMiwxMC4yMjItMjIuODMxLDIyLjgzMi0yMi44MzEgYzEyLjYxOCwwLDIyLjg0NywxMC4yMjksMjIuODQ3LDIyLjgzMUM0NDMuMjUzLDIzMi41NzIsNDMzLjAyNCwyNDIuODAxLDQyMC40MDYsMjQyLjgwMXoiPjwvcGF0aD4gPC9nPiA8L2c+PC9zdmc+)
![Genre](https://img.shields.io/badge/Genre-RPG-334155.svg?style=flat&logo=data:image/svg+xml;base64,PHN2ZyB2aWV3Qm94PSIwIDAgMjQgMjQiIGZpbGw9Im5vbmUiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgc3Ryb2tlPSIjZmZmZmZmIj48ZyBpZD0iU1ZHUmVwb19iZ0NhcnJpZXIiIHN0cm9rZS13aWR0aD0iMCI+PC9nPjxnIGlkPSJTVkdSZXBvX3RyYWNlckNhcnJpZXIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PC9nPjxnIGlkPSJTVkdSZXBvX2ljb25DYXJyaWVyIj4gPHBhdGggZmlsbC1ydWxlPSJldmVub2RkIiBjbGlwLXJ1bGU9ImV2ZW5vZGQiIGQ9Ik0yIDVDMiAzLjM0MzE1IDMuMzQzMTUgMiA1IDJIMTlDMjAuNjU2OSAyIDIyIDMuMzQzMTUgMjIgNVYxOUMyMiAyMC42NTY5IDIwLjY1NjkgMjIgMTkgMjJINUMzLjM0MzE1IDIyIDIgMjAuNjU2OSAyIDE5VjVaTTQgMTlWMTJIMTJWMjBINUM0LjQ0NzcyIDIwIDQgMTkuNTUyMyA0IDE5Wk0yMCAxMkgxMlY0SDE5QzE5LjU1MjMgNCAyMCA0LjQ0NzcyIDIwIDVWMTJaIiBmaWxsPSIjZmZmZmZmIj48L3BhdGg+IDwvZz48L3N2Zz4=)
![Windows](https://img.shields.io/badge/Platform-WIN-475569.svg?style=flat&logo=data:image/svg+xml;base64,PHN2ZyB2aWV3Qm94PSIwIDAgMjAgMjAiIHZlcnNpb249IjEuMSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB4bWxuczp4bGluaz0iaHR0cDovL3d3dy53My5vcmcvMTk5OS94bGluayIgZmlsbD0iI2ZmZmZmZiIgc3Ryb2tlPSIjZmZmZmZmIj48ZyBpZD0iU1ZHUmVwb19iZ0NhcnJpZXIiIHN0cm9rZS13aWR0aD0iMCI+PC9nPjxnIGlkPSJTVkdSZXBvX3RyYWNlckNhcnJpZXIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PC9nPjxnIGlkPSJTVkdSZXBvX2ljb25DYXJyaWVyIj4gPHRpdGxlPndpbmRvd3MgWyNmZmZmZmZdPC90aXRsZT4gPGRlc2M+Q3JlYXRlZCB3aXRoIFNrZXRjaC48L2Rlc2M+IDxkZWZzPiA8L2RlZnM+IDxnIGlkPSJQYWdlLTEiIHN0cm9rZT0ibm9uZSIgc3Ryb2tlLXdpZHRoPSIxIiBmaWxsPSJub25lIiBmaWxsLXJ1bGU9ImV2ZW5vZGQiPiA8ZyBpZD0iRHJpYmJibGUtTGlnaHQtUHJldmlldyIgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoLTYwLjAwMDAwMCwgLTc0MzkuMDAwMDAwKSIgZmlsbD0iI2ZmZmZmZiI+IDxnIGlkPSJpY29ucyIgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoNTYuMDAwMDAwLCAxNjAuMDAwMDAwKSI+IDxwYXRoIGQ9Ik0xMy4xNDU4NjQ3LDcyODkuNDM0MjYgQzEzLjE1MDg3NzIsNzI5MS40MzMxNiAxMy4xNTY4OTIyLDcyOTQuODI5MjkgMTMuMTYxOTA0OCw3Mjk3LjQ2ODg0IEMxNi43NzU5Mzk4LDcyOTcuOTU3NTcgMjAuMzg5OTc0OSw3Mjk4LjQ2MTMgMjMuOTk3OTk1LDcyOTkgQzIzLjk5Nzk5NSw3Mjk1Ljg0ODczIDI0LjAwMjAwNSw3MjkyLjcxMTQ2IDIzLjk5Nzk5NSw3Mjg5LjcxMzExIEMyMC4zODA5NTI0LDcyODkuNzEzMTEgMTYuNzY0OTEyMyw3Mjg5LjQzNDI2IDEzLjE0NTg2NDcsNzI4OS40MzQyNiBNNCw3Mjg5LjQzNTI2IEw0LDcyOTYuMjIxNTMgQzYuNzI1ODE0NTQsNzI5Ni41ODkzMyA5LjQ1MTYyOTA3LDcyOTYuOTQxMTMgMTIuMTcyNDMxMSw3Mjk3LjM0MjkxIEMxMi4xNzc0NDM2LDcyOTQuNzE3MzYgMTIuMTcwNDI2MSw3MjkyLjA5MDggMTIuMTcwNDI2MSw3Mjg5LjQ2NTI0IEM5LjQ0NjYxNjU0LDcyODkuNDcwMjQgNi43MjM4MDk1Miw3Mjg5LjQyNjI3IDQsNzI4OS40MzUyNiBNNCw3MjgxLjg0MzQ0IEw0LDcyODguNjEwNzEgQzYuNzI1ODE0NTQsNzI4OC42MTc3MSA5LjQ1MTYyOTA3LDcyODguNTc2NzMgMTIuMTc3NDQzNiw3Mjg4LjU3OTczIEMxMi4xNzU0Mzg2LDcyODUuOTYwMTcgMTIuMTc1NDM4Niw3MjgzLjM0MzYxIDEyLjE3MjQzMTEsNzI4MC43MjQwNSBDOS40NDQ2MTE1Myw3MjgxLjA2NDg2IDYuNzE2NzkxOTgsNzI4MS40MjU2NyA0LDcyODEuODQzNDQgTTI0LDcyODguNDcxNzkgQzIwLjM4Nzk2OTksNzI4OC40ODU3OCAxNi43NzU5Mzk4LDcyODguNTQwNzUgMTMuMTYxOTA0OCw3Mjg4LjU1MTc1IEMxMy4xNTk4OTk3LDcyODUuODg5MjEgMTMuMTU5ODk5Nyw3MjgzLjIyOTY3IDEzLjE2MTkwNDgsNzI4MC41NjkxNCBDMTYuNzY4OTIyMyw3MjgwLjAxODQ0IDIwLjM4Mzk1OTksNzI3OS41MDA3MiAyMy45OTc5OTUsNzI3OSBDMjQsNzI4Mi4xNTgyNiAyMy45OTc5OTUsNzI4NS4zMTM1MyAyNCw3Mjg4LjQ3MTc5IiBpZD0id2luZG93cy1bI2ZmZmZmZl0iPiA8L3BhdGg+IDwvZz4gPC9nPiA8L2c+IDwvZz48L3N2Zz4=)
![CMake](https://img.shields.io/badge/CMake-3.10%2B-c0392b?style=flat&logo=cmake&logoColor=white)
![OpenGL](https://img.shields.io/badge/OpenGL-v4.6-orange?style=flat&logo=opengl&logoColor=white)
![Vulkan](https://img.shields.io/badge/Vulkan-v1.4-red?style=flat&logo=vulkan&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-475569.svg?logo=data:image/svg+xml;base64,PHN2ZyBmaWxsPSIjZmZmZmZmIiB3aWR0aD0iMTY0cHgiIGhlaWdodD0iMTY0cHgiIHZpZXdCb3g9IjAgMCA1MTIuMDAgNTEyLjAwIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHN0cm9rZT0iI2ZmZmZmZiIgc3Ryb2tlLXdpZHRoPSIwLjAwNTEyIj48ZyBpZD0iU1ZHUmVwb19iZ0NhcnJpZXIiIHN0cm9rZS13aWR0aD0iMCI+PC9nPjxnIGlkPSJTVkdSZXBvX3RyYWNlckNhcnJpZXIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCIgc3Ryb2tlPSIjQ0NDQ0NDIiBzdHJva2Utd2lkdGg9IjMuMDcyIj48L2c+PGcgaWQ9IlNWR1JlcG9faWNvbkNhcnJpZXIiPjxwYXRoIGQ9Ik0yNTYgOEMxMTkuMDMzIDggOCAxMTkuMDMzIDggMjU2czExMS4wMzMgMjQ4IDI0OCAyNDggMjQ4LTExMS4wMzMgMjQ4LTI0OFMzOTIuOTY3IDggMjU2IDh6bTExNy4xMzQgMzQ2Ljc1M2MtMS41OTIgMS44NjctMzkuNzc2IDQ1LjczMS0xMDkuODUxIDQ1LjczMS04NC42OTIgMC0xNDQuNDg0LTYzLjI2LTE0NC40ODQtMTQ1LjU2NyAwLTgxLjMwMyA2Mi4wMDQtMTQzLjQwMSAxNDMuNzYyLTE0My40MDEgNjYuOTU3IDAgMTAxLjk2NSAzNy4zMTUgMTAzLjQyMiAzOC45MDRhMTIgMTIgMCAwIDEgMS4yMzggMTQuNjIzbC0yMi4zOCAzNC42NTVjLTQuMDQ5IDYuMjY3LTEyLjc3NCA3LjM1MS0xOC4yMzQgMi4yOTUtLjIzMy0uMjE0LTI2LjUyOS0yMy44OC02MS44OC0yMy44OC00Ni4xMTYgMC03My45MTYgMzMuNTc1LTczLjkxNiA3Ni4wODIgMCAzOS42MDIgMjUuNTE0IDc5LjY5MiA3NC4yNzcgNzkuNjkyIDM4LjY5NyAwIDY1LjI4LTI4LjMzOCA2NS41NDQtMjguNjI1IDUuMTMyLTUuNTY1IDE0LjA1OS01LjAzMyAxOC41MDggMS4wNTNsMjQuNTQ3IDMzLjU3MmExMi4wMDEgMTIuMDAxIDAgMCAxLS41NTMgMTQuODY2eiI+PC9wYXRoPjwvZz48L3N2Zz4=)
<br/>
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=lextpf_wild&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=lextpf_wild)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=lextpf_wild&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=lextpf_wild)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=lextpf_wild&metric=reliability_rating)](https://sonarcloud.io/summary/new_code?id=lextpf_wild)
<br/>
[![build](https://github.com/lextpf/rift/actions/workflows/build.yml/badge.svg)](https://github.com/lextpf/rift/actions/workflows/build.yml)
[![tests](https://github.com/lextpf/rift/actions/workflows/test.yml/badge.svg)](https://github.com/lextpf/rift/actions/workflows/test.yml)
<br/>
![Sponsor](https://img.shields.io/static/v1?label=sponsor&message=%E2%9D%A4&color=ff69b4&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCA2NDAgNjQwIj48IS0tIUZvbnQgQXdlc29tZSBQcm8gdjcuMi4wIGJ5IEBmb250YXdlc29tZSAtIGh0dHBzOi8vZm9udGF3ZXNvbWUuY29tIExpY2Vuc2UgLSBodHRwczovL2ZvbnRhd2Vzb21lLmNvbS9saWNlbnNlIChDb21tZXJjaWFsIExpY2Vuc2UpIENvcHlyaWdodCAyMDI2IEZvbnRpY29ucywgSW5jLi0tPjxwYXRoIG9wYWNpdHk9IjEiIGZpbGw9IiNmZjY5YjRmZiIgZD0iTTMyIDQ4MEwzMiA1NDRDMzIgNTYxLjcgNDYuMyA1NzYgNjQgNTc2TDM4NC41IDU3NkM0MTMuNSA1NzYgNDQxLjggNTY2LjcgNDY1LjIgNTQ5LjVMNTkxLjggNDU2LjJDNjA5LjYgNDQzLjEgNjEzLjQgNDE4LjEgNjAwLjMgNDAwLjNDNTg3LjIgMzgyLjUgNTYyLjIgMzc4LjcgNTQ0LjQgMzkxLjhMNDI0LjYgNDgwTDMxMiA0ODBDMjk4LjcgNDgwIDI4OCA0NjkuMyAyODggNDU2QzI4OCA0NDIuNyAyOTguNyA0MzIgMzEyIDQzMkwzODQgNDMyQzQwMS43IDQzMiA0MTYgNDE3LjcgNDE2IDQwMEM0MTYgMzgyLjMgNDAxLjcgMzY4IDM4NCAzNjhMMjMxLjggMzY4QzE5Ny45IDM2OCAxNjUuMyAzODEuNSAxNDEuMyA0MDUuNUw5OC43IDQ0OEw2NCA0NDhDNDYuMyA0NDggMzIgNDYyLjMgMzIgNDgweiIvPjxwYXRoIGZpbGw9InJnYmEoMjU1LCAyNTUsIDI1NSwgMS4wMCkiIGQ9Ik0yNTAuOSA2NEMyNzQuOSA2NCAyOTcuNSA3NS41IDMxMS42IDk1TDMyMCAxMDYuN0wzMjguNCA5NUMzNDIuNSA3NS41IDM2NS4xIDY0IDM4OS4xIDY0QzQzMC41IDY0IDQ2NCA5Ny41IDQ2NCAxMzguOUw0NjQgMTQxLjNDNDY0IDIwNS43IDM4MiAyNzQuNyAzNDEuOCAzMDQuNkMzMjguOCAzMTQuMyAzMTEuMyAzMTQuMyAyOTguMyAzMDQuNkMyNTguMSAyNzQuNiAxNzYgMjA1LjcgMTc2LjEgMTQxLjNMMTc2LjEgMTM4LjlDMTc2IDk3LjUgMjA5LjUgNjQgMjUwLjkgNjR6Ii8+PC9zdmc+)
</div>

**A modern 2.5D game engine** built with **C++23**, featuring dual *OpenGL 4.6* and *Vulkan 1.4* rendering backends that can be switched at runtime. It includes *dynamic world simulation* with a full day/night cycle, weather effects, and *NPC interactions*, alongside a **built-in level editor** for rapid content creation. Designed for **tile-based RPGs** with a focus on *performance*, *flexibility*, and **visual polish**.

<div align="center">
<br>

![Game](./PREVIEW.png)

</div>

> [!IMPORTANT]
> **Assets Not Included**
>
> This engine ships without game assets (sprites, tilesets, fonts, maps). To run the engine, you will need to provide your own:
> - Source free/open-licensed sprites and tilesets (e.g., from [OpenGameArt](https://opengameart.org/), [itch.io](https://itch.io/game-assets/free))
> - Place them in the `assets/` directory following the structure in [Project Structure](#project-structure)
> - Wire them into the engine by adjusting the loading paths (e.g., Game.cpp L233, L273)
>
> **The engine will not run without valid assets in place.**

```
/* ============================================================================================== *
 *                                                                                                
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠳⣶⡤
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠠⣾⣦⡀
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣈⣻⡧⢀
 *           :::::::::  ::::::::::: :::::::::: :::::::::::    ⢷⣦⣤⡀⠀⢀⣠⣤⡆⢰⣶⣶⣾⣿⣿⣷⣕⣡⡀
 *           :+:    :+:     :+:     :+:            :+:        ⠘⣿⣿⠇⠀⣦⡀⠉⠉⠈⠉⠁⢸⣿⣿⣿⣿⡿⠃
 *           +:+    +:+     +:+     +:+            +:+        ⠀⠀⠀⣀⣴⣿⣿⣄⣀⣀⣀⢀⣼⣿⣿⣿⠁
 *           +#++:++#:      +#+     :#::+::#       +#+        ⠀⠀⠀⠀⠉⢩⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡀
 *           +#+    +#+     +#+     +#+            +#+        ⠀⠀⠀⠀⠀⣸⣿⣿⡿⢻⣿⣿⣿⣿⡿⢿⠇
 *           #+#    #+#     #+#     #+#            #+#        ⠀⠀⠀⠀⢰⣿⣿⣿⠰⠙⠁⠈⣿⣿⠱⠘
 *           ###    ### ########### ###            ###        ⠀⠀⠀⠀⢸⡏⣾⡿⠁⠀⠀⠀⢿⣼⣷⠁
 *                                                            ⠀⠀⠀⠀⠘⠷⢿⣧⡀⠀⠀⠀⠈⠛⢿⣆
 *                                                            ⠀⠀⠀⠀⠀⠀⠀⠉⠉⠀⠀⠀⠀⠀⠀⠈
 *                                  << G A M E   E N G I N E >>                        
 *                                                                                                  
 * ============================================================================================== */
```

## Features

### Dual Rendering Backend
Switch between **OpenGL 4.6** and **Vulkan 1.4** at runtime with a single keypress (**F1**).

> [!WARNING]
> **Vulkan** is **work-in-progress**. Runtime switching can cause missing textures or visual glitches. **OpenGL is recommended** for now.

### Dynamic Day/Night Cycle
A complete time-of-day system drives ambient lighting, sky colors, celestial bodies, and atmospheric effects:

```mermaid
---
config:
  look: handDrawn
  theme: mc
  themeVariables:
    fontSize: 18px
  layout: elk
---
graph LR
    classDef night fill:#1a1a2e,stroke:#9aa4c0,stroke-width:2.5px,color:#ffffff,font-weight:800
    classDef dawn  fill:#614385,stroke:#d7a3ff,stroke-width:2.5px,color:#ffffff,font-weight:800
    classDef day   fill:#f39c12,stroke:#7a3e00,stroke-width:2.5px,color:#ffffff,font-weight:900
    classDef dusk  fill:#c0392b,stroke:#ff9a9a,stroke-width:2.5px,color:#ffffff,font-weight:800

    N["Night 🌙✨"]:::night --> D["Dawn 🌅🕊️"]:::dawn --> M["Morning ☀️🌿"]:::day --> A["Afternoon 🌞🕶️"]:::day --> DU["Dusk 🌇🔥"]:::dusk --> E["Evening 🌃🌟"]:::night --> N
```

- **Sun and moon god rays** with arc-based positioning
- **Star field** with shooting stars and atmospheric glow
- **Smooth color transitions** between time periods

### Multi-Layer Tilemap System
Efficient tile-based world rendering with 8 configurable layers:

```mermaid
---
config:
  look: handDrawn
  theme: mc
  themeVariables:
    fontSize: 18px
  layout: elk
---
flowchart LR
  classDef ov fill:#2e1f5e,stroke:#8b5cf6,stroke-width:2.5px,color:#e2e8f0,font-weight:800
  classDef fg fill:#134e3a,stroke:#10b981,stroke-width:2.5px,color:#e2e8f0,font-weight:800
  classDef ob fill:#4a3520,stroke:#f59e0b,stroke-width:2.5px,color:#e2e8f0,font-weight:800
  classDef gr fill:#1e3a5f,stroke:#3b82f6,stroke-width:2.5px,color:#e2e8f0,font-weight:800

  O["7–6 Overlay 🌦️✨<br/>UI - Weather 🧭"]:::ov
  F["5–4 Foreground 🧍‍♂️⚔️<br/>Front of Entities 👑"]:::fg
  B["3–2 Objects 🏠🪨🌲<br/>Buildings - Rocks - Trees 🧱"]:::ob
  G["1 Detail 🍃🛤️🪴<br/>Grass - Paths - Deco 🎨"]:::gr
  T["0 Ground 🗺️🟫<br/>Base Terrain 🌍"]:::gr

  O --> F --> B --> G --> T

```

### Advanced Rendering Features

```mermaid
---
config:
  look: handDrawn
  theme: mc
  themeVariables:
    fontSize: 18px
  layout: elk
---
flowchart LR
  classDef batch fill:#1e3a5f,stroke:#3b82f6,stroke-width:2.5px,color:#e2e8f0,font-weight:800
  classDef sort fill:#134e3a,stroke:#10b981,stroke-width:2.5px,color:#e2e8f0,font-weight:800
  classDef proj fill:#4a3520,stroke:#f59e0b,stroke-width:2.5px,color:#e2e8f0,font-weight:800
  classDef fx fill:#2e1f5e,stroke:#8b5cf6,stroke-width:2.5px,color:#e2e8f0,font-weight:800

  subgraph Pipeline["Rendering Pipeline 🎨"]
    B["Sprite Batching 📦<br/>Draw Call Optimization 🚀"]:::batch
    Y["Y-Sorting 🧭<br/>Depth Ordering 🪜"]:::sort
    P["Perspective 👁️‍🗨️<br/>World Curvature 🌍"]:::proj
    N["No-Projection 🏗️<br/>Upright Structures 🧱"]:::proj
    FX["Particles ✨<br/>Zone Spawning 🌪️"]:::fx
  end

  B --> Y
  Y --> P
  Y --> N
  P --> FX
  N --> FX
```

- **Sprite batching** groups draw calls for optimal GPU efficiency
- **Y-sorting** ensures correct depth ordering of entities and tiles
- **Perspective projection** adds world curvature and vanishing point effects
- **No-projection tiles** keep buildings and signs upright
- **Particle systems** spawn effects within defined zones

### In-Game Level Editor
Press **E** to toggle a full-featured editor:
- Tile placement with multi-tile selection and rotation
- Collision and navigation map painting
- Tile elevation for height variation
- NPC placement and patrol route configuration
- Animation definition tools

### Entity System
- **Player character** with 8-directional movement and animation
- **NPCs** with autonomous patrol behavior and pathfinding
- **Dialogue system** supporting branching conversations with quests

## Architecture

```mermaid
---
config:
  look: handDrawn
  theme: mc
  themeVariables:
    fontSize: 18px
  layout: elk
---
graph LR
    classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef render fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
    classDef world fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef entity fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    Game((Game)):::core

    subgraph Rendering["Rendering"]
        IRenderer[IRenderer]:::render
        OpenGL[OpenGLRenderer]:::render
        Vulkan[VulkanRenderer]:::render
    end

    subgraph World["World"]
        Tilemap[Tilemap]:::world
        Collision[CollisionMap]:::world
        Navigation[NavigationMap]:::world
    end

    subgraph Entities["Entities"]
        Player[PlayerCharacter]:::entity
        NPC[NonPlayerCharacter]:::entity
    end

    Game --> Rendering
    Game --> World
    Game --> Entities
```

## Technology Stack

| Component      | Technology              |
|----------------|-------------------------|
| Language       | C++23                   |
| Graphics       | OpenGL 4.6 / Vulkan 1.4 |
| Windowing      | GLFW 3.3+               |
| Math           | GLM                     |
| Image Loading  | stb_image               |
| Font Rendering | FreeType 2              |
| Build System   | CMake 3.10+             |

## Quick Start

### Prerequisites
- CMake 3.10+
- C++23 compatible compiler (MSVC 2022+)
- OpenGL 4.6 compatible GPU (or Vulkan 1.4 capable)

### Windows

```powershell
# 1. Clone the repository
git clone https://github.com/lextpf/rift.git
cd rift

# 2. Run setup script to download dependencies
.\setup.ps1

# 3. Build the project
.\build.bat

# 4. Run the game
.\build\Release\rift.exe
```

## Controls

<details>

<summary>General Keybinds</summary>

### Gameplay

| Key         | Action                           |
|-------------|----------------------------------|
| W/A/S/D     | Move player (8-directional)      |
| Shift       | Run (1.5x speed)                 |
| B           | Toggle bicycle mode (2.0x speed) |
| F           | Talk to NPC when facing          |
| X           | Copy/restore NPC appearance      |
| C           | Cycle characters                 |
| Ctrl+Scroll | Zoom camera                      |
| Arrow Keys  | Pan camera                       |
| Z           | Reset zoom to 1.0x               |
| Space       | Free camera mode                 |

### Dialogue

| Key            |  Action                     |
|----------------|-----------------------------|
| W/S or Up/Down | Navigate dialogue options   |
| Enter/Space    | Confirm selection / advance |
| Escape         | End dialogue                |

### Movement Modes

| Mode    | Speed    | Collision              |
|---------|----------|------------------------|
| Walking | 100 px/s | Strict (full hitbox)   |
| Running | 150 px/s | Relaxed (center point) |
| Bicycle | 200 px/s | Relaxed (center point) |

### Editor

| Key          | Action                             |
|--------------|------------------------------------|
| E            | Toggle editor mode                 |
| 1-8          | Select tilemap layer               |
| T            | Toggle tile picker                 |
| R            | Rotate selection 90 deg            |
| Delete       | Remove tile at cursor              |
| S            | Save map to JSON                   |
| L            | Load map from JSON                 |
| M            | Toggle navigation editing          |
| N            | Toggle NPC placement               |
| H            | Toggle elevation editing           |
| B            | Toggle billboard projection        |
| Y            | Toggle Y-sort editing              |
| J            | Toggle particle zone editing       |
| K            | Toggle animated tile editing       |
| X            | Toggle corner cut blocking on tile |
| , / .        | Cycle types (NPC/particle/anim)    |
| Left Click   | Place tile/NPC/zone                |
| Right Click  | Toggle collision/navigation        |
| Arrows       | Pan tile picker                    |
| Shift+Arrows | Pan tile picker (fast)             |
| Scroll       | Pan tile picker                    |
| Ctrl+Scroll  | Zoom                               |

### Debug and Visual

| Key     | Action                                                 |
|---------|--------------------------------------------------------|
| F1      | Switch renderer (OpenGL/Vulkan)                        |
| F2      | Toggle debug overlays (collision, navigation, anchors) |
| F3      | Toggle FPS/position display                            |
| F4      | Toggle 3D globe effect                                 |
| F5      | Cycle time of day (day/evening/night/morning)          |
| Up/Down | Adjust 3D globe intensity                              |

</details>

For detailed editor controls, see the [Editor Guide](docs/EDITOR.md).

## Documentation

```mermaid
---
config:
  look: handDrawn
  theme: mc
  themeVariables:
    fontSize: 18px
  layout: elk
---
graph LR
    classDef guide fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef tech fill:#134e3a,stroke:#10b981,color:#e2e8f0

    subgraph Guides
        Setup[Setup]:::guide
        Building[Building]:::guide
        Editor[Editor]:::guide
    end

    subgraph Technical
        Arch[Architecture]:::tech
        Render[Rendering]:::tech
        Time[Time System]:::tech
        Collision[Collision]:::tech
    end

    Setup --> Building --> Editor
    Arch --> Render
    Arch --> Time
    Arch --> Collision
```

| Document                                         | Description                                         |
|--------------------------------------------------|-----------------------------------------------------|
| **[Setup Guide](docs/SETUP.md)**                 | Install dependencies and configure your environment |
| **[Building Guide](docs/BUILDING.md)**           | Compile on Windows                                  |
| **[Architecture](docs/ARCHITECTURE.md)**         | Engine design, game loop, system relationships      |
| **[Rendering Pipeline](docs/RENDERING.md)**      | Coordinate systems, transformations, batching       |
| **[Time System](docs/TIME_SYSTEM.md)**           | Day/night cycle, celestial mechanics, lighting      |
| **[Collision & Pathfinding](docs/COLLISION.md)** | AABB collision, navigation, NPC AI                  |
| **[Editor Guide](docs/EDITOR.md)**               | Level editor usage and tools                        |

### Generating API Docs

```powershell
# Install Doxygen
# Windows: choco install doxygen

# Generate documentation
doxygen Doxyfile

# Open docs/html/index.html in your browser
```

### File Formats

- **`.json`** - Map data (layers, tiles, NPCs)
- **`.png`** - Sprites (32-bit RGBA)
- **`.ttf`** - Fonts (TrueType)
- **`.vert/.frag`** - Shaders (GLSL 450)

## Project Structure

```
rift/
|-- src/                      # Source code
|   |-- main.cpp              # Entry point
|   |-- Game.cpp/h            # Core game loop and systems
|   |-- IRenderer.h           # Renderer interface
|   |-- OpenGLRenderer.*      # OpenGL backend
|   |-- VulkanRenderer.*      # Vulkan backend
|   |-- Tilemap.*             # World and tile management
|   |-- TimeManager.*         # Day/night cycle
|   |-- SkyRenderer.*         # Celestial rendering
|   |-- PlayerCharacter.*     # Player entity
|   |-- NonPlayerCharacter.*  # NPC entities
|   +-- ...
|-- assets/                   # Game assets
|   |-- fonts/                # Font files (TTF) for UI and text rendering
|   |-- non-player/           # NPC and enemy sprite images
|   |-- overworld/            # World environment, tilesets, and map images
|   |-- player/               # Player character sprite sheets and animations
|   +-- particles/            # Small particle effect images (sparks, dust, etc.)
|-- shaders/                  # GLSL shaders
|   +-- sprite.vert/frag      # Sprite rendering
|-- docs/                     # Documentation
+-- external/                 # Third-party dependencies
```

## Contributing

Contributions are welcome! Please read the [Contributing Guidelines](CONTRIBUTING.md) before submitting pull requests.

### Development Workflow
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. ~~Run tests and~~ ensure the build passes
5. Commit with descriptive messages
6. Push to your fork and open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [GLFW](https://www.glfw.org/) - Window and input handling
- [GLM](https://github.com/g-truc/glm) - Mathematics library
- [stb_image](https://github.com/nothings/stb) - Image loading
- [FreeType](https://freetype.org/) - Font rendering
- [Vulkan SDK](https://vulkan.lunarg.com/) - Vulkan development tools
- [Claude](https://claude.ai/) - AI coding assistant by Anthropic
- [Codex](https://openai.com/index/openai-codex/) - AI coding assistant by OpenAI
- [DeviantArt](https://www.deviantart.com/) - Pixel art for characters and tilesets
- [Sora](https://openai.com/sora/) - Particle effect generation
























