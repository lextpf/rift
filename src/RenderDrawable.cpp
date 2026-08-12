#include "RenderDrawable.hpp"

#include "AnimationState.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "NpcRender.hpp"
#include "NpcSprite.hpp"
#include "PlayerModes.hpp"
#include "PlayerRender.hpp"
#include "PlayerSprite.hpp"
#include "Transform.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_map>
#include <utility>

namespace
{
// A character is one queue item even though the current renderer still submits
// its two sprite halves. The thunk executes those submissions consecutively, so
// no tile can ever sort between feet, body, hair, equipment, or taller future
// character art.
void AddEntityDrawable(std::vector<Drawable>& list,
                       const ecs::registry& world,
                       ecs::entity entity,
                       glm::vec2 feetPos,
                       SupportSurface supportSurface,
                       float supportHeight,
                       int surfaceRegionId,
                       std::uint8_t tieBias,
                       void (*drawEntity)(const Drawable&, IRenderer&, glm::vec2))
{
    Drawable item;
    item.cls = DrawableClass::Entity;
    item.phase = DrawablePhase::YSorted;
    item.sortY = feetPos.y;
    item.supportHeight = supportHeight;
    item.surfaceRegionId = surfaceRegionId;
    item.supportSurface = supportSurface;
    item.tieBias = tieBias;
    item.world = &world;
    item.handle = entity;
    item.drawEntity = drawEntity;
    list.push_back(item);
}
}  // namespace

void AddNpcDrawable(std::vector<Drawable>& list,
                    const ecs::registry& world,
                    ecs::entity npc,
                    glm::vec2 feetPos,
                    SupportSurface supportSurface,
                    float supportHeight,
                    int surfaceRegionId,
                    std::uint8_t tieBias)
{
    // Capture-less lambda -> function pointer. Re-resolves the NPC's render
    // components from (world, handle) at draw time. Both halves are submitted
    // inside this one thunk, making the character atomic to the render queue.
    void (*thunk)(const Drawable&, IRenderer&, glm::vec2) =
        +[](const Drawable& d, IRenderer& r, glm::vec2 cam)
    {
        auto [xf, elev, facing, anim, sprite] =
            d.world->get<Transform, Elevation, Facing, AnimationState, NpcSprite>(d.handle);
        NpcRender::DrawHalf(*d.world, r, cam, false, xf, elev, facing, anim, sprite);
        NpcRender::DrawHalf(*d.world, r, cam, true, xf, elev, facing, anim, sprite);
    };

    AddEntityDrawable(
        list, world, npc, feetPos, supportSurface, supportHeight, surfaceRegionId, tieBias, thunk);
}

void AddPlayerDrawable(std::vector<Drawable>& list,
                       const ecs::registry& world,
                       ecs::entity player,
                       glm::vec2 feetPos,
                       SupportSurface supportSurface,
                       float supportHeight,
                       int surfaceRegionId,
                       std::uint8_t tieBias)
{
    void (*thunk)(const Drawable&, IRenderer&, glm::vec2) =
        +[](const Drawable& d, IRenderer& r, glm::vec2 cam)
    {
        auto [xf, elev, facing, anim, modes, sprite] =
            d.world->get<Transform, Elevation, Facing, AnimationState, PlayerModes, PlayerSprite>(
                d.handle);
        PlayerRender::DrawHalf(*d.world, r, cam, false, xf, elev, facing, anim, modes, sprite);
        PlayerRender::DrawHalf(*d.world, r, cam, true, xf, elev, facing, anim, modes, sprite);
    };

    AddEntityDrawable(list,
                      world,
                      player,
                      feetPos,
                      supportSurface,
                      supportHeight,
                      surfaceRegionId,
                      tieBias,
                      thunk);
}

void SortDrawables(std::vector<Drawable>& list)
{
    std::stable_sort(list.begin(), list.end(), DrawableDepthLess);
    if (list.size() < 2)
    {
        return;
    }

    struct RegionNodes
    {
        std::vector<size_t> tiles;
        std::vector<size_t> backgroundSurfaceTiles;
        std::vector<size_t> authoredYSortTiles;
        std::vector<size_t> groundActors;
        std::vector<size_t> elevatedActors;
    };

    std::unordered_map<int, RegionNodes> regions;
    for (size_t index = 0; index < list.size(); ++index)
    {
        const Drawable& drawable = list[index];
        if (drawable.surfaceRegionId < 0)
        {
            continue;
        }

        RegionNodes& region = regions[drawable.surfaceRegionId];
        if (drawable.cls == DrawableClass::Tile)
        {
            region.tiles.push_back(index);
            if (drawable.tile.authoredYSort)
            {
                region.authoredYSortTiles.push_back(index);
            }
            else if (drawable.phase == DrawablePhase::Background)
            {
                region.backgroundSurfaceTiles.push_back(index);
            }
        }
        else if (drawable.supportSurface == SupportSurface::Ground)
        {
            region.groundActors.push_back(index);
        }
        else
        {
            region.elevatedActors.push_back(index);
        }
    }

    std::vector<std::vector<size_t>> outgoing(list.size());
    std::vector<size_t> incomingCount(list.size(), 0);
    size_t edgeCount = 0;
    auto addEdge = [&](size_t before, size_t after)
    {
        outgoing[before].push_back(after);
        ++incomingCount[after];
        ++edgeCount;
    };

    for (const auto& [regionId, region] : regions)
    {
        (void)regionId;

        // Underpass: only actors whose feet are actually inside this elevation
        // footprint move behind its artwork. A character merely beside the ramp
        // has no region id and therefore keeps ordinary authored Y sorting.
        for (size_t actor : region.groundActors)
        {
            for (size_t tile : region.tiles)
            {
                addEdge(actor, tile);
            }
        }

        // Deck/ramp: inferred background surface art stays below actors on the
        // elevated support. Explicit ySortPlus/ySortMinus tiles are deliberately
        // not constrained here; their authored top/bottom railing behavior wins.
        for (size_t tile : region.backgroundSurfaceTiles)
        {
            for (size_t actor : region.elevatedActors)
            {
                addEdge(tile, actor);
            }
        }

        // Authored Y-sort on elevated artwork is local to actors supported by
        // that elevation region. This preserves top/bottom railing behavior on
        // the deck without letting the same railing occlude a ground actor
        // merely standing beside the ramp.
        for (size_t tile : region.authoredYSortTiles)
        {
            for (size_t actor : region.elevatedActors)
            {
                if (DrawableYSortLess(list[tile], list[actor]))
                {
                    addEdge(tile, actor);
                }
                else
                {
                    addEdge(actor, tile);
                }
            }
        }
    }

    if (edgeCount == 0)
    {
        return;
    }

    // Stable topological sort: baseline painter index is the priority whenever
    // two nodes are unconstrained, so the graph changes only relationships the
    // local under/on-surface rules explicitly require.
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    for (size_t index = 0; index < incomingCount.size(); ++index)
    {
        if (incomingCount[index] == 0)
        {
            ready.push(index);
        }
    }

    std::vector<size_t> order;
    order.reserve(list.size());
    while (!ready.empty())
    {
        const size_t current = ready.top();
        ready.pop();
        order.push_back(current);
        for (size_t next : outgoing[current])
        {
            --incomingCount[next];
            if (incomingCount[next] == 0)
            {
                ready.push(next);
            }
        }
    }

    if (order.size() != list.size())
    {
        // The semantic roles above are acyclic by construction. Keep the valid
        // baseline order if future roles accidentally introduce a cycle.
        return;
    }

    std::vector<Drawable> ordered;
    ordered.reserve(list.size());
    for (size_t index : order)
    {
        ordered.push_back(std::move(list[index]));
    }
    list = std::move(ordered);
}
