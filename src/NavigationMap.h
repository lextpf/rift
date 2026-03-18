#pragma once

#include "BoolGrid.h"

/**
 * @class NavigationMap
 * @brief Boolean grid for per-tile NPC walkability flags in 2D tile-based worlds.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * NavigationMap stores walkability flags for a 2D tile grid. Inherits all
 * storage and access logic from BoolGrid, adding semantic method names
 * for the navigation domain.
 *
 * @tparam Container Template for the storage container (e.g., `std::vector`).
 *                   The container is instantiated as `Container<bool>`.
 *
 * @par Usage
 * @code{.cpp}
 * NavigationMap<std::vector> nav;
 * nav.Resize(64, 64);
 * nav[10][20] = true;
 * nav.SetNavigation(10, 20, true);
 * if (nav.GetNavigation(10, 20)) { ... }
 * if (nav[10, 20]) { ... }  // C++23 multidimensional subscript
 * @endcode
 *
 * @par Design Philosophy
 * Separating navigation from collision provides several benefits:
 * 1. **NPC Containment**: Keep NPCs in designated areas
 * 2. **Patrol Routes**: Create predictable patrol paths
 * 3. **Level Design**: Restrict NPCs without collision
 *
 * @par Bounds Handling
 * - **Read**: Out-of-bounds returns `false` (not walkable)
 * - **Write**: Out-of-bounds silently ignored
 *
 * @see BoolGrid For full implementation details
 * @see CollisionMap Similar structure for player collision
 * @see ColumnProxy For 2D array syntax implementation
 */
template <template <typename...> class Container>
    requires RandomAccessContainerOf<Container<bool>, bool> &&
             requires(Container<bool>& c, std::size_t i) {
                 c.resize(i, false);
                 { c.begin() };
                 { c.end() };
             }
class NavigationMap : public BoolGrid<Container>
{
public:
    using BoolGrid<Container>::BoolGrid;
    using BoolGrid<Container>::operator[];

    /// @brief Proxy type aliases for backwards compatibility.
    using NavigationColumn = typename BoolGrid<Container>::Column;
    using ConstNavigationColumn = typename BoolGrid<Container>::ConstColumn;

    /**
     * @brief Set walkability flag for a tile.
     * @param x        Column (out-of-bounds ignored).
     * @param y        Row (out-of-bounds ignored).
     * @param walkable `true` if NPCs can walk here.
     */
    constexpr void SetNavigation(int x, int y, bool walkable) noexcept
    {
        this->Set(x, y, walkable);
    }

    /**
     * @brief Query if a tile is walkable by NPCs.
     * @param x Column (out-of-bounds returns `false`).
     * @param y Row (out-of-bounds returns `false`).
     * @return `true` if walkable, `false` otherwise.
     */
    [[nodiscard]] constexpr bool GetNavigation(int x, int y) const noexcept
    {
        return this->Get(x, y);
    }

    /**
     * @brief Get flat indices of all walkable tiles.
     * @return Vector of indices where navigation is `true`.
     */
    [[nodiscard]] std::vector<int> GetNavigationIndices() const { return this->GetTrueIndices(); }

    /**
     * @brief Count walkable tiles.
     * @return Number of tiles where navigation is `true`.
     */
    [[nodiscard]] int GetNavigationCount() const { return this->GetTrueCount(); }
};
