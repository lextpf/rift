#pragma once

#include "ColumnProxy.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <vector>

/**
 * @class CollisionMap
 * @brief Boolean grid for per-tile collision flags in 2D tile-based worlds.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * CollisionMap stores collision flags for a 2D tile grid. The element type is
 * always `bool`, but the underlying container can be customized via template
 * template parameter for different performance characteristics.
 *
 * @tparam Container Template for the storage container (e.g., `std::vector`).
 *                   The container is instantiated as `Container<bool>`.
 *
 * @par Usage
 * @code{.cpp}
 * CollisionMap<std::vector> col;
 * col.Resize(64, 64);
 * col[10][20] = true;
 * if (col.HasCollision(10, 20)) { ... }
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
 * - **Read**: Out-of-bounds returns `false` (passable)
 * - **Write**: Out-of-bounds silently ignored
 *
 * @par Thread Safety
 * Not thread-safe. Concurrent reads are safe; writes require synchronization.
 *
 * @see ColumnProxy For 2D array syntax implementation
 * @see NavigationMap Similar structure for NPC walkability
 */
template <template <typename...> class Container>
    requires RandomAccessContainerOf<Container<bool>, bool>
             // Second constraint: verify std::ranges::begin/end work
             && requires(Container<bool>& c, std::size_t i) {
                    c.resize(i, false);
                    { c.begin() };
                    { c.end() };
                }
class CollisionMap
{
public:
    /// @brief The concrete container type (`Container<bool>`).
    using container_type = Container<bool>;

    /// @brief Element type (always `bool`).
    using value_type = bool;

    /// @brief Proxy type for `map[x][y]` syntax.
    using CollisionColumn = ColumnProxy<container_type, value_type, false>;
    using ConstCollisionColumn = ConstColumnProxy<container_type, value_type, false>;

    /**
     * @brief Construct an empty collision map.
     * @post GetWidth() == 0 && GetHeight() == 0
     */
    constexpr CollisionMap() noexcept = default;
    ~CollisionMap() = default;

    /// @brief Copy and move constructors/assignments.
    CollisionMap(CollisionMap&&) noexcept = default;
    CollisionMap& operator=(CollisionMap&&) noexcept = default;
    CollisionMap(const CollisionMap&) = default;
    CollisionMap& operator=(const CollisionMap&) = default;

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
        m_Collision.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                           false);
    }

    /**
     * @brief Set collision flag for a tile.
     *
     * @param x         Column (out-of-bounds ignored).
     * @param y         Row (out-of-bounds ignored).
     * @param collision `true` if blocking, `false` if passable.
     */
    constexpr void SetCollision(int x, int y, bool collision) noexcept
    {
        if (x >= 0 && x < m_Width && y >= 0 && y < m_Height)
            m_Collision[static_cast<std::size_t>(y * m_Width + x)] = collision;
    }

    /**
     * @brief Query if a tile blocks movement.
     *
     * @param x Column (out-of-bounds returns `false`).
     * @param y Row (out-of-bounds returns `false`).
     * @return `true` if blocking, `false` if passable or out-of-bounds.
     */
    [[nodiscard]] constexpr bool HasCollision(int x, int y) const noexcept
    {
        if (x >= 0 && x < m_Width && y >= 0 && y < m_Height)
            return static_cast<bool>(m_Collision[static_cast<std::size_t>(y * m_Width + x)]);
        return false;
    }

    /**
     * @brief Get flat indices of all blocking tiles.
     *
     * Convert index to coordinates: `x = i % w`, `y = i / w`
     *
     * @return Vector of indices where collision is `true`.
     */
    [[nodiscard]] std::vector<int> GetCollisionIndices() const
    {
        std::vector<int> indices;
        indices.reserve(m_Collision.size());
        for (auto [i, val] : std::views::enumerate(m_Collision))
            if (val)
                indices.push_back(static_cast<int>(i));
        return indices;
    }

    /**
     * @brief Clear all flags to `false` (passable).
     */
    void Clear() { std::ranges::fill(m_Collision, false); }

    /// @brief Get width in tiles.
    [[nodiscard]] constexpr int GetWidth() const noexcept { return m_Width; }

    /// @brief Get height in tiles.
    [[nodiscard]] constexpr int GetHeight() const noexcept { return m_Height; }

    /**
     * @brief Count blocking tiles.
     * @return Number of tiles where collision is `true`.
     */
    [[nodiscard]] int GetCollisionCount() const
    {
        return static_cast<int>(std::ranges::count(m_Collision, true));
    }

    /// @brief Get read-only access to underlying data.
    [[nodiscard]] constexpr const container_type& GetData() const noexcept { return m_Collision; }

    /**
     * @brief Replace all map data in one call.
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
        m_Collision = data;
        return true;
    }

    /// @brief 2D array access (mutable): `map[x][y] = true`
    [[nodiscard]] constexpr CollisionColumn operator[](int x) noexcept
    {
        return CollisionColumn(&m_Collision, &m_Width, &m_Height, x);
    }

    /// @brief 2D array access on const-qualified map.
    [[nodiscard]] constexpr ConstCollisionColumn operator[](int x) const noexcept
    {
        return ConstCollisionColumn(&m_Collision, &m_Width, &m_Height, x);
    }

private:
    container_type m_Collision{};
    int m_Width{0};
    int m_Height{0};
};
