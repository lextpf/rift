#pragma once

#include "BoolGrid.h"

/**
 * @class CollisionMap
 * @brief Boolean grid for per-tile collision flags in 2D tile-based worlds.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * CollisionMap stores collision flags for a 2D tile grid. Inherits all
 * storage and access logic from BoolGrid, adding semantic method names
 * for the collision domain.
 *
 * @tparam Container Template for the storage container (e.g., `std::vector`).
 *                   The container is instantiated as `Container<bool>`.
 *
 * @par Usage
 * @code{.cpp}
 * CollisionMap<std::vector> col;
 * col.Resize(64, 64);
 * col[10][20] = true;
 * col.SetCollision(10, 20, true);
 * if (col.HasCollision(10, 20)) { ... }
 * if (col[10, 20]) { ... }  // C++23 multidimensional subscript
 * @endcode
 *
 * @par Bounds Handling
 * - **Read**: Out-of-bounds returns `false` (passable)
 * - **Write**: Out-of-bounds silently ignored
 *
 * @see BoolGrid For full implementation details
 * @see NavigationMap Similar structure for NPC walkability
 * @see ColumnProxy For 2D array syntax implementation
 */
template <template <typename...> class Container>
    requires RandomAccessContainerOf<Container<bool>, bool> &&
             requires(Container<bool>& c, std::size_t i) {
                 c.resize(i, false);
                 { c.begin() };
                 { c.end() };
             }
class CollisionMap : public BoolGrid<Container>
{
public:
    using BoolGrid<Container>::BoolGrid;
    using BoolGrid<Container>::operator[];

    /// @brief Proxy type aliases for backwards compatibility.
    using CollisionColumn = typename BoolGrid<Container>::Column;
    using ConstCollisionColumn = typename BoolGrid<Container>::ConstColumn;

    /**
     * @brief Set collision flag for a tile.
     * @param x         Column (out-of-bounds ignored).
     * @param y         Row (out-of-bounds ignored).
     * @param collision `true` if blocking, `false` if passable.
     */
    constexpr void SetCollision(int x, int y, bool collision) noexcept
    {
        this->Set(x, y, collision);
    }

    /**
     * @brief Query if a tile blocks movement.
     * @param x Column (out-of-bounds returns `false`).
     * @param y Row (out-of-bounds returns `false`).
     * @return `true` if blocking, `false` if passable or out-of-bounds.
     */
    [[nodiscard]] constexpr bool HasCollision(int x, int y) const noexcept
    {
        return this->Get(x, y);
    }

    /**
     * @brief Get flat indices of all blocking tiles.
     * @return Vector of indices where collision is `true`.
     */
    [[nodiscard]] std::vector<int> GetCollisionIndices() const { return this->GetTrueIndices(); }

    /**
     * @brief Count blocking tiles.
     * @return Number of tiles where collision is `true`.
     */
    [[nodiscard]] int GetCollisionCount() const { return this->GetTrueCount(); }
};
