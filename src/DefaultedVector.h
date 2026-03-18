#pragma once

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <vector>

/**
 * @concept resettable_container
 * @brief Type supporting parallel resize and reset-to-default operations.
 * @author Alex (https://github.com/lextpf)
 *
 * Satisfied by any type with `resize(size_t)` and `resetToDefault()` methods,
 * including the defaulted_vector template below.
 */
template <typename F>
concept resettable_container = requires(F& f, size_t n) {
    f.resize(n);
    f.resetToDefault();
};

/**
 * @class defaulted_vector
 * @brief Vector wrapper with a compile-time default value for parallel array patterns.
 * @author Alex (https://github.com/lextpf)
 *
 * Wraps `std::vector<T>` and associates a compile-time default value used by
 * resize_all / reset_all fold expressions. Provides transparent `operator[]`,
 * `size()`, and iteration so existing access patterns work unchanged.
 *
 * @tparam T       Element type.
 * @tparam Default Compile-time default value (C++20 NTTP: supports int, float, bool).
 *
 * @par Usage
 * @code{.cpp}
 * defaulted_vector<int, -1> tiles;
 * tiles.resize(100);       // 100 elements, all -1
 * tiles[42] = 7;
 * tiles.resetToDefault();  // all 100 elements back to -1
 * @endcode
 *
 * @par Proxy Handling
 * Uses `decltype(auto)` for `operator[]` to correctly forward
 * `std::vector<bool>`'s proxy reference type.
 */
template <typename T, auto Default>
class defaulted_vector
{
public:
    using value_type = T;
    static constexpr auto default_value = static_cast<T>(Default);

    /// @brief Index access. Returns `T&` for most types, proxy for `bool`.
    /// Uses C++23 deducing this to unify const/non-const overloads.
    [[nodiscard]] decltype(auto) operator[](this auto&& self, size_t i) { return self.m_Data[i]; }

    [[nodiscard]] size_t size() const noexcept { return m_Data.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_Data.empty(); }

    /// @brief Iterator access. Deducing this forwards const-ness automatically.
    [[nodiscard]] auto begin(this auto&& self) noexcept { return self.m_Data.begin(); }
    [[nodiscard]] auto end(this auto&& self) noexcept { return self.m_Data.end(); }

    /// @brief Resize to n elements; new slots initialized to default_value.
    void resize(size_t n) { m_Data.resize(n, default_value); }

    /// @brief Replace contents with n copies of value.
    void assign(size_t n, const T& value) { m_Data.assign(n, value); }

    /// @brief Fill every element with default_value (size unchanged).
    void resetToDefault() { std::ranges::fill(m_Data, default_value); }

private:
    std::vector<T> m_Data;
};

/**
 * @brief Resize all resettable_container members to the same size.
 * @author Alex (https://github.com/lextpf)
 *
 * Uses a fold expression to call `resize(n)` on each container in a single statement.
 *
 * @code{.cpp}
 * defaulted_vector<int, -1> tiles;
 * defaulted_vector<float, 0.0f> rotation;
 * resize_all(100, tiles, rotation);  // both resized to 100
 * @endcode
 *
 * @param n          New size for all containers.
 * @param containers Pack of resettable_container references.
 */
template <resettable_container... Containers>
void resize_all(size_t n, Containers&... containers)
{
    (containers.resize(n), ...);
}

/**
 * @brief Reset all resettable_container members to their compile-time defaults.
 * @author Alex (https://github.com/lextpf)
 *
 * Uses a fold expression to call `resetToDefault()` on each container.
 *
 * @param containers Pack of resettable_container references.
 */
template <resettable_container... Containers>
void reset_all(Containers&... containers)
{
    (containers.resetToDefault(), ...);
}
