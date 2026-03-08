#pragma once

#include "ColumnProxy.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <vector>

/**
 * @class NavigationMap
 * @brief Boolean grid for per-tile NPC walkability flags in 2D tile-based worlds.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * NavigationMap stores walkability flags for a 2D tile grid. The element type is
 * always `bool`, but the underlying container can be customized via template
 * template parameter for different performance characteristics.
 *
 * @tparam Container Template for the storage container (e.g., `std::vector`).
 *                   The container is instantiated as `Container<bool>`.
 *
 * @par Usage
 * @code{.cpp}
 * NavigationMap<std::vector> nav;
 * nav.Resize(64, 64);
 * nav[10][20] = true;
 * if (nav.GetNavigation(10, 20)) { ... }
 * @endcode
 *
 * @par Storage Options
 * | Container        | Memory      | Access Speed | Notes                    |
 * |------------------|-------------|--------------|--------------------------|
 * | `std::vector`    | Bit-packed  | Good         | Default, mem efficient   |
 * | `std::deque`     | Chunked     | Good         | Better for huge maps     |
 *
 * @par Design Philosophy
 * Separating navigation from collision provides several benefits:
 * 1. **NPC Containment**: Keep NPCs in designated areas
 * 2. **Patrol Routes**: Create predictable patrol paths
 * 3. **Level Design**: Restrict NPCs without collision
 *
 * @par Memory Layout
 * Data is stored in row-major order:
 * @code
 *     Column:  0   1   2   3
 *            +---+---+---+---+
 *   Row 0:   | 0 | 1 | 2 | 3 |
 *            +---+---+---+---+
 *   Row 1:   | 4 | 5 | 6 | 7 |
 *            +---+---+---+---+
 * @endcode
 *
 * @par Coordinate System
 * - **x**: Column (horizontal), range [0, width), increasing rightward
 * - **y**: Row (vertical), range [0, height), increasing downward
 * - Index formula: `i = y * w + x`
 *
 * @par Bounds Handling
 * - **Read**: Out-of-bounds returns `false` (not walkable)
 * - **Write**: Out-of-bounds silently ignored
 *
 * @par Thread Safety
 * Not thread-safe. Concurrent reads are safe; writes require synchronization.
 *
 * @see ColumnProxy For 2D array syntax implementation
 * @see CollisionMap Similar structure for player collision
 */
template <template <typename...> class Container>
    requires RandomAccessContainerOf<Container<bool>, bool> &&
             requires(Container<bool>& c, std::size_t i) {
                 c.resize(i, false);
                 { c.begin() };
                 { c.end() };
             }
class NavigationMap
{
public:
    /// @brief The concrete container type (`Container<bool>`).
    using container_type = Container<bool>;

    /// @brief Element type (always `bool`).
    using value_type = bool;

    /// @brief Proxy type for `map[x][y]` syntax.
    using NavigationColumn = ColumnProxy<container_type, value_type, false>;
    using ConstNavigationColumn = ConstColumnProxy<container_type, value_type, false>;

    /**
     * @brief Construct an empty navigation map.
     * @post GetWidth() == 0 && GetHeight() == 0
     */
    constexpr NavigationMap() noexcept = default;
    ~NavigationMap() = default;

    /// @brief Copy and move constructors/assignments.
    NavigationMap(NavigationMap&&) noexcept = default;
    NavigationMap& operator=(NavigationMap&&) noexcept = default;
    NavigationMap(const NavigationMap&) = default;
    NavigationMap& operator=(const NavigationMap&) = default;

    /**
     * @brief Resize to new dimensions, clearing all flags to `false`.
     *
     * @param width  New width in tiles.
     * @param height New height in tiles.
     */
    void Resize(int width, int height)
    {
        m_Width = width;
        m_Height = height;
        m_Navigation.resize(static_cast<std::size_t>(width * height), false);
    }

    /**
     * @brief Set walkability flag for a tile.
     *
     * @param x        Column (out-of-bounds ignored).
     * @param y        Row (out-of-bounds ignored).
     * @param walkable `true` if NPCs can walk here.
     */
    constexpr void SetNavigation(int x, int y, bool walkable) noexcept
    {
        if (x >= 0 && x < m_Width && y >= 0 && y < m_Height)
            m_Navigation[static_cast<std::size_t>(y * m_Width + x)] = walkable;
    }

    /**
     * @brief Query if a tile is walkable by NPCs.
     *
     * @param x Column (out-of-bounds returns `false`).
     * @param y Row (out-of-bounds returns `false`).
     * @return `true` if walkable, `false` otherwise.
     */
    [[nodiscard]] constexpr bool GetNavigation(int x, int y) const noexcept
    {
        if (x >= 0 && x < m_Width && y >= 0 && y < m_Height)
            return static_cast<bool>(m_Navigation[static_cast<std::size_t>(y * m_Width + x)]);
        return false;
    }

    /**
     * @brief Get flat indices of all walkable tiles.
     *
     * Convert index to coordinates: `x = i % w`, `y = i / w`
     *
     * @return Vector of indices where navigation is `true`.
     */
    [[nodiscard]] std::vector<int> GetNavigationIndices() const
    {
        std::vector<int> indices;
        indices.reserve(m_Navigation.size());
        for (auto [i, val] : std::views::enumerate(m_Navigation))
            if (val)
                indices.push_back(static_cast<int>(i));
        return indices;
    }

    /**
     * @brief Clear all flags to `false` (not walkable).
     */
    void Clear() { std::ranges::fill(m_Navigation, false); }

    /// @brief Get width in tiles.
    [[nodiscard]] constexpr int GetWidth() const noexcept { return m_Width; }

    /// @brief Get height in tiles.
    [[nodiscard]] constexpr int GetHeight() const noexcept { return m_Height; }

    /**
     * @brief Count walkable tiles.
     * @return Number of tiles where navigation is `true`.
     */
    [[nodiscard]] int GetNavigationCount() const
    {
        return static_cast<int>(std::ranges::count(m_Navigation, true));
    }

    /// @brief Get read-only access to underlying data.
    [[nodiscard]] constexpr const container_type& GetData() const noexcept { return m_Navigation; }

    /**
     * @brief Replace all map data in one call.
     *
     * @param data   New data (must
     * have size == w * h).
     * @param width  New width.
     * @param height New height.
     *
     * @return `true` if valid, `false` if size mismatch.
     */
    bool SetData(const container_type& data, int width, int height)
    {
        if (data.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
            return false;
        m_Width = width;
        m_Height = height;
        m_Navigation = data;
        return true;
    }

    /// @brief 2D array access (mutable): `map[x][y] = true`
    [[nodiscard]] constexpr NavigationColumn operator[](int x) noexcept
    {
        return NavigationColumn(&m_Navigation, &m_Width, &m_Height, x);
    }

    /// @brief 2D array access on const-qualified map.
    [[nodiscard]] constexpr ConstNavigationColumn operator[](int x) const noexcept
    {
        return ConstNavigationColumn(&m_Navigation, &m_Width, &m_Height, x);
    }

private:
    container_type m_Navigation{};
    int m_Width{0};
    int m_Height{0};
};
