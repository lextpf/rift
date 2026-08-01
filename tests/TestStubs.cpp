// Stub definitions for symbols referenced by code that is linked into the
// test executable but never actually invoked at runtime. These exist purely
// to satisfy the linker without dragging the full game into the test build.
//
// - Game::SwitchRenderer: referenced by Cmd_RendererSet, but the renderer.set
//   command path is integration-only and never exercised in unit tests.
// - Editor::CalculateRotatedSourceTile / GetCompensatedTileRotation /
//   CalculateParticleZoneRect: these have real definitions in src/EditorInput.cpp,
//   which CMakeLists.txt keeps out of TEST_LIB_SOURCES on purpose (ProcessMouseInput
//   pulls in the dialogue-tree builders). EditorRendering.cpp IS linked and calls
//   them from its overlay methods, which the tests never invoke - hence these
//   stubs. Defining them in a separate TU is legal because C++ access checks apply
//   at the call site, not the definition site.
//
//   WARNING: if EditorInput.cpp is ever added to TEST_LIB_SOURCES, delete these
//   three definitions in the same change, or the test binary fails to link on
//   duplicate symbols.

#include "../src/Editor.hpp"
#include "../src/Game.hpp"
#include "../src/RendererAPI.hpp"

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
