#pragma once

#include "ColumnProxy.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <vector>

/**
 * @class BoolGrid
 * @brief Generic boolean grid for per-tile flags in 2D tile-based worlds.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * BoolGrid stores boolean flags for a 2D tile grid. The element type is
 * always `bool`, but the underlying container can be customized via template
 * template parameter for different performance characteristics.
 *
 * This is the shared implementation behind CollisionMap and NavigationMap,
 * which add semantic method names for their respective domains.
 *
 * @tparam Container Template for the storage container (e.g., `std::vector`).
 *                   The container is instantiated as `Container<bool>`.
 *
 * @par Usage
 * @code{.cpp}
 * BoolGrid<std::vector> grid;
 * grid.Resize(64, 64);
 * grid[10][20] = true;          // ColumnProxy access
 * bool v = grid[10, 20];        // C++23 multidimensional subscript
 * grid.Set(10, 20, false);      // Named setter
 * if (grid.Get(10, 20)) { ... } // Named getter
 * @endcode
 *
 * @par Storage Options
 * | Container        | Memory      | Access Speed | Notes                    |
 * |------------------|-------------|--------------|--------------------------|
 * | `std::vector`    | Bit-packed  | Good         | Default, mem efficient   |
 * | `std::deque`     | Chunked     | Good         | Better for huge maps     |
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
 * - **Read**: Out-of-bounds returns `false`
 * - **Write**: Out-of-bounds silently ignored
 *
 * @par Thread Safety
 * Not thread-safe. Concurrent reads are safe; writes require synchronization.
 *
 * @see ColumnProxy For 2D array syntax implementation
 * @see CollisionMap Semantic wrapper for player collision
 * @see NavigationMap Semantic wrapper for NPC walkability
 */
template <template <typename...> class Container>
    requires RandomAccessContainerOf<Container<bool>, bool>
             // Second constraint: verify std::ranges::begin/end work
             && requires(Container<bool>& c, std::size_t i) {
                    c.resize(i, false);
                    { c.begin() };
                    { c.end() };
                }
class BoolGrid
{
public:
    /// @brief The concrete container type (`Container<bool>`).
    using container_type = Container<bool>;

    /// @brief Element type (always `bool`).
    using value_type = bool;

    /// @brief Proxy type for `grid[x][y]` syntax.
    using Column = ColumnProxy<container_type, value_type, false>;
    using ConstColumn = ColumnProxy<container_type, value_type, false, false>;

    /**
     * @brief Construct an empty grid.
     * @post GetWidth() == 0 && GetHeight() == 0
     */
    constexpr BoolGrid() noexcept = default;
    ~BoolGrid() = default;

    /// @brief Copy and move constructors/assignments.
    BoolGrid(BoolGrid&&) noexcept = default;
    BoolGrid& operator=(BoolGrid&&) noexcept = default;
    BoolGrid(const BoolGrid&) = default;
    BoolGrid& operator=(const BoolGrid&) = default;

    /**
     * @brief Resize to new dimensions, clearing all flags to `false`.
     *
     * @param width  New width in tiles.
     * @param height New height in tiles.
     */
    void Resize(int width, int height)
    {
        if (width < 0)
            width = 0;
        if (height < 0)
            height = 0;
        m_Width = width;
        m_Height = height;
        m_Data.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), false);
    }

    /**
     * @brief Set a flag at a tile position.
     *
     * @param x     Column (out-of-bounds ignored).
     * @param y     Row (out-of-bounds ignored).
     * @param value Flag value to set.
     */
    constexpr void Set(int x, int y, bool value) noexcept
    {
        if (x >= 0 && x < m_Width && y >= 0 && y < m_Height)
            m_Data[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_Width) +
                   static_cast<std::size_t>(x)] = value;
    }

    /**
     * @brief Query the flag at a tile position.
     *
     * @param x Column (out-of-bounds returns `false`).
     * @param y Row (out-of-bounds returns `false`).
     * @return Flag value, or `false` if out-of-bounds.
     */
    [[nodiscard]] constexpr bool Get(int x, int y) const noexcept
    {
        if (x >= 0 && x < m_Width && y >= 0 && y < m_Height)
            return static_cast<bool>(
                m_Data[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_Width) +
                       static_cast<std::size_t>(x)]);
        return false;
    }

    /**
     * @brief Get flat indices of all `true` tiles.
     *
     * Convert index to coordinates: `x = i % w`, `y = i / w`
     *
     * @return Vector of indices where the flag is `true`.
     */
    [[nodiscard]] std::vector<int> GetTrueIndices() const
    {
        std::vector<int> indices;
        indices.reserve(m_Data.size());
        for (auto [i, val] : std::views::enumerate(m_Data))
            if (val)
                indices.push_back(static_cast<int>(i));
        return indices;
    }

    /**
     * @brief Clear all flags to `false`.
     */
    void Clear() { std::ranges::fill(m_Data, false); }

    /// @brief Get width in tiles.
    [[nodiscard]] constexpr int GetWidth() const noexcept { return m_Width; }

    /// @brief Get height in tiles.
    [[nodiscard]] constexpr int GetHeight() const noexcept { return m_Height; }

    /**
     * @brief Count `true` tiles.
     * @return Number of tiles where the flag is `true`.
     */
    [[nodiscard]] int GetTrueCount() const
    {
        return static_cast<int>(std::ranges::count(m_Data, true));
    }

    /// @brief Get read-only access to underlying data.
    [[nodiscard]] constexpr const container_type& GetData() const noexcept { return m_Data; }

    /**
     * @brief Replace all grid data in one call.
     *
     * @param data   New data (must have size == w * h).
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
        m_Data = data;
        return true;
    }

    /**
     * @brief C++23 multidimensional subscript for direct read access.
     *
     * @code{.cpp}
     * bool v = grid[10, 20];
     * @endcode
     *
     * @param x Column (out-of-bounds returns `false`).
     * @param y Row (out-of-bounds returns `false`).
     * @return Flag value, or `false` if out-of-bounds.
     */
    [[nodiscard]] constexpr bool operator[](int x, int y) const noexcept { return Get(x, y); }

    /// @brief 2D proxy access (mutable): `grid[x][y] = true`
    [[nodiscard]] constexpr Column operator[](int x) noexcept
    {
        return Column(&m_Data, &m_Width, &m_Height, x);
    }

    /// @brief 2D proxy access on const-qualified grid.
    [[nodiscard]] constexpr ConstColumn operator[](int x) const noexcept
    {
        return ConstColumn(&m_Data, &m_Width, &m_Height, x);
    }

protected:
    container_type m_Data{};
    int m_Width{0};
    int m_Height{0};
};
