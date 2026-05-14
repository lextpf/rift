#pragma once

#include <cstddef>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

/**
 * @brief CRTP base providing common ToString/FromString for EnumTraits.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * @tparam E       Enum type.
 * @tparam Derived The concrete EnumTraits<E> specialization.
 *
 * Derived must provide:
 * - `static constexpr size_t Count`
 * - `static constexpr std::string_view Names[]`
 */
template <typename E, typename Derived>
struct EnumTraitsBase
{
    static constexpr std::string_view ToString(E value)
    {
        auto i = std::to_underlying(value);
        return static_cast<size_t>(i) < Derived::Count ? Derived::Names[i] : "Unknown";
    }

    static constexpr std::optional<E> FromString(std::string_view name)
    {
        for (size_t i = 0; i < Derived::Count; ++i)
            if (Derived::Names[i] == name)
                return static_cast<E>(i);
        return std::nullopt;
    }
};

/**
 * @brief Compile-time reflection traits for enum types.
 * @author Alex (https://github.com/lextpf)
 *
 * @tparam E The enum type to reflect.
 *
 * Specialize this template, inheriting from EnumTraitsBase, to provide:
 * - `Count`      - number of enumerators
 * - `Names[]`    - string names indexed by underlying value
 *
 * ToString and FromString are provided automatically by EnumTraitsBase.
 *
 * @par Example Specialization
 * @code
 * enum class Color { Red = 0, Green = 1, Blue = 2 };
 *
 * template<>
 * struct EnumTraits<Color> : EnumTraitsBase<Color, EnumTraits<Color>> {
 *     static constexpr size_t Count = 3;
 *     static constexpr std::string_view Names[] = { "Red", "Green", "Blue" };
 * };
 * @endcode
 */
template <typename E>
struct EnumTraits;

/**
 * @brief Advance an enum value to the next enumerator, wrapping at Count.
 * @author Alex (https://github.com/lextpf)
 *
 * @tparam E Enum type with a valid EnumTraits specialization.
 *
 * Requires contiguous zero-based enumerators (0, 1, ..., Count-1).
 * Uses C++23 `std::to_underlying` for clean enum-to-integer conversion.
 *
 * @param value Current enumerator.
 * @return Next enumerator, wrapping from the last back to 0.
 *
 * @par Example
 * @code
 * auto next = NextEnum(CharacterType::BW1_MALE);  // BW1_FEMALE
 * auto wrap = NextEnum(CharacterType::CC_FEMALE);  // BW1_MALE
 * @endcode
 */
template <typename E>
    requires requires { EnumTraits<E>::Count; }
constexpr E NextEnum(E value)
{
    return static_cast<E>((std::to_underlying(value) + 1) % EnumTraits<E>::Count);
}

/**
 * @brief Range of all enumerator values for an enum with EnumTraits.
 * @author Alex (https://github.com/lextpf)
 *
 * @tparam E Enum type with a valid EnumTraits specialization.
 *
 * @par Example
 * @code
 * for (auto pt : EnumValues<ParticleType>())
 *     std::cout << EnumTraits<ParticleType>::ToString(pt) << "\n";
 * @endcode
 */
template <typename E>
    requires requires { EnumTraits<E>::Count; }
constexpr auto EnumValues()
{
    return std::views::iota(size_t{0}, EnumTraits<E>::Count) |
           std::views::transform([](size_t i) { return static_cast<E>(i); });
}

/**
 * @brief Invoke a callable for each enumerator value.
 * @author Alex (https://github.com/lextpf)
 *
 * @tparam E  Enum type with a valid EnumTraits specialization.
 * @tparam Fn Callable accepting E.
 */
template <typename E, typename Fn>
    requires requires { EnumTraits<E>::Count; }
constexpr void ForEachEnum(Fn&& fn)
{
    for (auto v : EnumValues<E>())
        fn(v);
}
