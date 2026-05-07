// Stub definitions for symbols referenced by code that is linked into the
// test executable but never actually invoked at runtime. These exist purely
// to satisfy the linker without dragging the full game into the test build.
//
// - Game::SwitchRenderer: referenced by Cmd_RendererSet, but the renderer.set
//   command path is integration-only and never exercised in unit tests.
// - Editor::CalculateRotatedSourceTile / GetCompensatedTileRotation /
//   CalculateParticleZoneRect: referenced by EditorRendering.cpp's overlay
//   methods, which the tests never call. Defining these in a separate TU is
//   legal because C++ access checks apply at the call site, not the
//   definition site.

#include "../src/Editor.h"
#include "../src/Game.h"
#include "../src/RendererAPI.h"

bool Game::SwitchRenderer(RendererAPI /*api*/)
{
    return false;
}

void Editor::CalculateRotatedSourceTile(int /*dx*/, int /*dy*/, int& sourceDx, int& sourceDy) const
{
    sourceDx = 0;
    sourceDy = 0;
}

float Editor::GetCompensatedTileRotation() const
{
    return 0.0f;
}

Editor::TileZoneRect Editor::CalculateParticleZoneRect(float /*worldX*/,
                                                       float /*worldY*/,
                                                       int /*tileWidth*/,
                                                       int /*tileHeight*/) const
{
    return TileZoneRect{};
}
