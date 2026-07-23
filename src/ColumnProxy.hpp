#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

/**
 * @brief Concept for random-access containers holding element type T.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Accepts both ordinary containers and `std::vector<bool>`-style containers whose `operator[]`
 * returns a proxy reference rather than a true `T&`. The concept requires the container to be
 * readable and writable through `operator[]`, and `size()` to return something convertible to
 * `std::size_t`.
 *
 * @tparam C  Container type to check.
 * @tparam T  Element type.
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
 * @ingroup World
 *
 * This wrapper enables `map[x][y] = value` syntax even for containers like
 * `std::vector<bool>` whose `operator[]` returns a proxy reference rather
 * than a true `T&`. For out-of-bounds access, assignments are silently
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
    /**
     * @brief Bind the proxy to one element slot.
     *
     * Non-owning: @p data must outlive the proxy, which is intended to be a short-lived
     * temporary in a `grid[x][y]` expression. When @p valid is false the proxy is inert -
     * assignment is discarded and reads yield DefaultValue - so @p index is ignored and
     * callers pass 0 rather than an out-of-range value.
     *
     * @param data  Container the element lives in; never dereferenced when @p valid is false.
     * @param index Flat row-major index into @p data.
     * @param valid Whether the coordinates were in bounds.
     */
    constexpr RefProxy(C* data, std::size_t index, bool valid) noexcept
        : m_Data(data),
          m_Index(index),
          m_Valid(valid)
    {
    }

    /// @brief Write through to the element; a no-op when out-of-bounds.
    constexpr RefProxy& operator=(const T& value) noexcept
    {
        if (m_Valid)
            (*m_Data)[m_Index] = value;
        return *this;
    }

    /// @brief Read the element, or DefaultValue when out-of-bounds.
    [[nodiscard]] constexpr operator T() const noexcept
    {
        return m_Valid ? static_cast<T>((*m_Data)[m_Index]) : DefaultValue;
    }

private:
    C* m_Data;            ///< Non-owning; the caller guarantees it outlives the proxy.
    std::size_t m_Index;  ///< Flat row-major index; meaningless unless m_Valid.
    bool m_Valid;         ///< False makes the proxy inert instead of out-of-bounds.
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
 * @par Memory layout
 * Data is stored in row-major order:
 * @f[
 * i = y \times w + x
 * @f]
 *
 * @par Bounds handling
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
    data_ptr m_Data;  ///< Non-owning pointer to the flat container.
    /// Pointers, not values, so the proxy sees a live resize of the owning grid. All three
    /// pointers must outlive the proxy; it is meant to be a temporary within one expression.
    const int* m_Width;
    const int* m_Height;
    int m_X;  ///< Column this proxy was built for; bounds-checked on each row access.
};

/// @brief Backwards-compatible alias for read-only ColumnProxy.
template <typename C, typename T, T DefaultValue = T{}>
using ConstColumnProxy = ColumnProxy<C, T, DefaultValue, false>;
