#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

/**
 * @concept RandomAccessContainerOf
 * @brief Concept for containers with random access supporting element type T.
 * @author Alex (https://github.com/lextpf)
 *
 * Handles both regular containers and `std::vector<T>`
 * which returns a proxy reference type from `operator[]`.
 *
 * @tparam C Container type to check
 * @tparam T Element type
 */
template<typename C, typename T>
concept RandomAccessContainerOf = requires(C& c, const C& cc, std::size_t i, T val) {
    { c[i] = val };                                     // Assignable from T
    { static_cast<T>(cc[i]) };                          // Convertible to T
    { cc.size() } -> std::convertible_to<std::size_t>;
};

/**
 * @class RefProxy
 * @brief Proxy for element access, handling both real refs and proxy types.
 * @author Alex (https://github.com/lextpf)
 *
 * This wrapper enables `map[x][y] = value` syntax.
 * For out-of-bounds access, assignments are silently
 * discarded and reads return DefaultValue.
 *
 * @tparam C Container type
 * @tparam T Element type
 * @tparam DefaultValue Value returned for out-of-bounds reads
 */
template<typename C, typename T, T DefaultValue>
class RefProxy
{
public:
    constexpr RefProxy(C* data, std::size_t index, bool valid) noexcept
        : m_Data(data), m_Index(index), m_Valid(valid) {}

    constexpr RefProxy& operator=(const T& value) noexcept
    {
        if (m_Valid)
            (*m_Data)[m_Index] = value;
        return *this;
    }

    [[nodiscard]] constexpr operator T() const noexcept
    {
        return m_Valid ? static_cast<T>((*m_Data)[m_Index]) : DefaultValue;
    }

private:
    C* m_Data;
    std::size_t m_Index;
    bool m_Valid;
};

/**
 * @class ConstColumnProxy
 * @brief Read-only proxy for `map[x][y]` syntax on const containers.
 *
 * Provides bounds-checked reads only. Out-of-bounds reads return DefaultValue.
 *
 * @tparam C Container type satisfying RandomAccessContainerOf<T>
 * @tparam T Element type
 * @tparam DefaultValue Value returned for out-of-bounds reads
 */
template<typename C, typename T, T DefaultValue = T{}>
    requires RandomAccessContainerOf<C, T>
class ConstColumnProxy
{
public:
    using container_type = C;
    using value_type = T;

    constexpr ConstColumnProxy(const C* data, const int* width, const int* height, int x) noexcept
        : m_Data(data), m_Width(width), m_Height(height), m_X(x) {}

    [[nodiscard]] constexpr T operator[](int y) const noexcept
    {
        if (m_X >= 0 && m_X < *m_Width && y >= 0 && y < *m_Height)
            return static_cast<T>((*m_Data)[static_cast<std::size_t>(y * (*m_Width) + m_X)]);
        return DefaultValue;
    }

private:
    const C* m_Data;
    const int* m_Width;
    const int* m_Height;
    int m_X;
};

/**
 * @class ColumnProxy
 * @brief Generic proxy class enabling `map[x][y]` syntax for flat 2D data.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * ColumnProxy is a lightweight proxy that captures a column index (x) and
 * provides row access via a second `operator[]`.
 *
 * @tparam C            Container type satisfying RandomAccessContainerOf<T>
 * @tparam T            Element type
 * @tparam DefaultValue Value returned for out-of-bounds reads (NTTP)
 *
 * @par Usage
 * @code{.cpp}
 * std::vector<bool> flags(64 * 64, false);
 * int w = 64, h = 64;
 * ColumnProxy<std::vector<bool>, bool, false> boolCol(&flags, &w, &h, 10);
 * boolCol[20] = true;  // Write
 * if (boolCol[20]) {}  // Read
 * @endcode
 *
 * @par Memory Layout
 * Data is stored in row-major order:
 * @f[
 * i = y \times w + x
 * @f]
 *
 * @par Bounds Handling
 * - **Read**: Out-of-bounds returns DefaultValue
 * - **Write**: Out-of-bounds silently ignored
 *
 * @see CollisionMap, NavigationMap, Tilemap
 */
template<typename C, typename T, T DefaultValue = T{}>
    requires RandomAccessContainerOf<C, T>
class ColumnProxy
{
public:
    using container_type = C;
    using value_type = T;

    /**
     * @brief Construct proxy for mutable container access.
     * @param data   Pointer to underlying container.
     * @param width  Pointer to grid width.
     * @param height Pointer to grid height.
     * @param x      Column index for this proxy.
     */
    constexpr ColumnProxy(C* data, const int* width, const int* height, int x) noexcept
        : m_Data(data), m_Width(width), m_Height(height), m_X(x) {}

    /**
     * @brief Access element at row y (mutable).
     * @param y Row index.
     * @return Proxy that can be assigned to; out-of-bounds assignments are discarded.
     */
    [[nodiscard]] constexpr RefProxy<C, T, DefaultValue> operator[](int y) noexcept
    {
        const bool valid = (m_X >= 0 && m_X < *m_Width && y >= 0 && y < *m_Height);
        const auto index = static_cast<std::size_t>(y * (*m_Width) + m_X);
        return RefProxy<C, T, DefaultValue>(m_Data, valid ? index : 0, valid);
    }

    /**
     * @brief Access element at row y (const).
     * @param y Row index.
     * @return Element value, or DefaultValue if out-of-bounds.
     */
    [[nodiscard]] constexpr T operator[](int y) const noexcept
    {
        if (m_X >= 0 && m_X < *m_Width && y >= 0 && y < *m_Height)
            return static_cast<T>((*m_Data)[static_cast<std::size_t>(y * (*m_Width) + m_X)]);
        return DefaultValue;
    }

private:
    C* m_Data;
    const int* m_Width;
    const int* m_Height;
    int m_X;
};
