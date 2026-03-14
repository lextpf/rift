#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

/**
 * @brief Compile-time reflection traits for enum types.
 * @tparam E The enum type to reflect.
 *
 * Specialize this template to provide:
 * - `Count`      — number of enumerators
 * - `Names[]`    — string names indexed by underlying value
 * - `ToString()` — enumerator to string_view
 * - `FromString()` — string_view to optional enumerator
 *
 * @par Example Specialization
 * @code
 * enum class Color { Red = 0, Green = 1, Blue = 2 };
 *
 * template<>
 * struct EnumTraits<Color> {
 *     static constexpr size_t Count = 3;
 *     static constexpr std::string_view Names[] = { "Red", "Green", "Blue" };
 *
 *     static constexpr std::string_view ToString(Color v) {
 *         auto i = static_cast<size_t>(v);
 *         return i < Count ? Names[i] : "Unknown";
 *     }
 *
 *     static constexpr std::optional<Color> FromString(std::string_view name) {
 *         for (size_t i = 0; i < Count; ++i)
 *             if (Names[i] == name) return static_cast<Color>(i);
 *         return std::nullopt;
 *     }
 * };
 * @endcode
 */
template <typename E>
struct EnumTraits;
