#pragma once

#include "EnumTraits.hpp"

/**
 * @enum CharacterType
 * @brief Available player character sprite variants.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Each character type owns three sprite sheets: walking, which also holds the idle pose;
 * running; and bicycle.
 */
enum class CharacterType
{
    BW1_MALE = 0,    ///< Black and White 1 male protagonist.
    BW1_FEMALE = 1,  ///< Black and White 1 female protagonist.
    BW2_MALE = 2,    ///< Black and White 2 male protagonist.
    BW2_FEMALE = 3,  ///< Black and White 2 female protagonist.
    CC_FEMALE = 4    ///< Crystal Clear female character.
};

/**
 * @brief Compile-time reflection for @ref CharacterType.
 * @ingroup Entities
 *
 * Supplies the count and the string names that the base template turns into
 * ToString / FromString / iteration. @ref Names is index-parallel to the enumerator values,
 * so both must be extended together and in the same order.
 *
 * Adding a variant means touching this specialization as well as the enum; the static
 * assertion below is the guard that turns a forgotten update into a build error rather than
 * an out-of-range name lookup at runtime.
 */
template <>
struct EnumTraits<CharacterType> : EnumTraitsBase<CharacterType, EnumTraits<CharacterType>>
{
    static constexpr size_t Count = 5;  ///< Number of enumerators; also the Names extent.
    /// Identifier spelling per enumerator, in declaration order. Console and asset-registry
    /// lookups round-trip through these, so renaming one is a user-visible change.
    static constexpr std::string_view Names[] = {
        "BW1_MALE", "BW1_FEMALE", "BW2_MALE", "BW2_FEMALE", "CC_FEMALE"};

    // Pins the last enumerator to Count - 1, which only holds while the values stay a dense
    // 0-based run.
    static_assert(std::to_underlying(CharacterType::CC_FEMALE) == Count - 1,
                  "Update EnumTraits<CharacterType> when adding new CharacterType values");
};
