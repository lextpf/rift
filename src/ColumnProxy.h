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
template <typename C, typename T>
concept RandomAccessContainerOf = requires(C& c, const C& cc, std::size_t i, T val) {
    { c[i] = val };             // Assignable from T
    { static_cast<T>(cc[i]) };  // Convertible to T
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
template <typename C, typename T, T DefaultValue>
class RefProxy
{
public:
    constexpr RefProxy(C* data, std::size_t index, bool valid) noexcept
        : m_Data(data),
          m_Index(index),
          m_Valid(valid)
    {
    }

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
 * @class ColumnProxy
 * @brief Generic proxy class enabling `map[x][y]` syntax for flat 2D data.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * ColumnProxy is a lightweight proxy that captures a column index (x) and
 * provides row access via a second `operator[]`.
 *
 * When `Mutable` is `true` (default), both read and write access are
 * available. When `Mutable` is `false`, only read access is provided,
 * and the proxy stores a `const C*` pointer. This eliminates the need
 * for a separate ConstColumnProxy class.
 *
 * @tparam C            Container type satisfying RandomAccessContainerOf<T>
 * @tparam T            Element type
 * @tparam DefaultValue Value returned for out-of-bounds reads (NTTP)
 * @tparam Mutable      If true, mutable element access via RefProxy is enabled
 *
 * @par Usage
 * @code{.cpp}
 * std::vector<bool> flags(64 * 64, false);
 * int w = 64, h = 64;
 *
 * ColumnProxy<std::vector<bool>, bool, false> boolCol(&flags, &w, &h, 10);
 * boolCol[20] = true;                         // Write (Mutable=true)
 * if (boolCol[20]) {}                         // Read
 *
 * ColumnProxy<std::vector<bool>, bool, false, false> readOnly(&flags, &w, &h, 10);
 * bool v = readOnly[20];                      // Read-only (Mutable=false)
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
template <typename C, typename T, T DefaultValue = T{}, bool Mutable = true>
    requires RandomAccessContainerOf<C, T>
class ColumnProxy
{
public:
    using container_type = C;
    using value_type = T;

    /// Pointer type: `C*` when Mutable, `const C*` otherwise.
    using data_ptr = std::conditional_t<Mutable, C*, const C*>;

    /**
     * @brief Construct proxy for container access.
     * @param data   Pointer to underlying container (const when Mutable=false).
     * @param width  Pointer to grid width.
     * @param height Pointer to grid height.
     * @param x      Column index for this proxy.
     */
    constexpr ColumnProxy(data_ptr data, const int* width, const int* height, int x) noexcept
        : m_Data(data),
          m_Width(width),
          m_Height(height),
          m_X(x)
    {
    }

    /**
     * @brief Access element at row y (mutable).
     * @param y Row index.
     * @return Proxy that can be assigned to; out-of-bounds assignments are discarded.
     */
    [[nodiscard]] constexpr RefProxy<C, T, DefaultValue> operator[](int y) noexcept
        requires Mutable
    {
        const bool valid = (m_X >= 0 && m_X < *m_Width && y >= 0 && y < *m_Height);
        const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(*m_Width) +
                           static_cast<std::size_t>(m_X);
        return RefProxy<C, T, DefaultValue>(m_Data, valid ? index : 0, valid);
    }

    /**
     * @brief Access element at row y (read-only).
     * @param y Row index.
     * @return Element value, or DefaultValue if out-of-bounds.
     */
    [[nodiscard]] constexpr T operator[](int y) const noexcept
    {
        if (m_X >= 0 && m_X < *m_Width && y >= 0 && y < *m_Height)
            return static_cast<T>(
                (*m_Data)[static_cast<std::size_t>(y) * static_cast<std::size_t>(*m_Width) +
                          static_cast<std::size_t>(m_X)]);
        return DefaultValue;
    }

private:
    data_ptr m_Data;
    const int* m_Width;
    const int* m_Height;
    int m_X;
};

/// @brief Backwards-compatible alias for read-only ColumnProxy.
template <typename C, typename T, T DefaultValue = T{}>
using ConstColumnProxy = ColumnProxy<C, T, DefaultValue, false>;
