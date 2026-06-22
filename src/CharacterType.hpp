#pragma once

#include "EnumTraits.hpp"

/**
 * @enum CharacterType
 * @brief Available player character sprite variants.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Each character type has its own set of sprite sheets:
 * - Walking sprite sheet (idle + walk animations)
 * - Running sprite sheet (sprint animation)
 * - Bicycle sprite sheet (cycling animation)
 */
enum class CharacterType
{
    BW1_MALE = 0,    ///< Black & White 1 Male protagonist
    BW1_FEMALE = 1,  ///< Black & White 1 Female protagonist
    BW2_MALE = 2,    ///< Black & White 2 Male protagonist
    BW2_FEMALE = 3,  ///< Black & White 2 Female protagonist
    CC_FEMALE = 4    ///< Crystal Clear Female character
};

/// Compile-time reflection for CharacterType.
template <>
struct EnumTraits<CharacterType> : EnumTraitsBase<CharacterType, EnumTraits<CharacterType>>
{
    static constexpr size_t Count = 5;
    static constexpr std::string_view Names[] = {
        "BW1_MALE", "BW1_FEMALE", "BW2_MALE", "BW2_FEMALE", "CC_FEMALE"};

    static_assert(std::to_underlying(CharacterType::CC_FEMALE) == Count - 1,
                  "Update EnumTraits<CharacterType> when adding new CharacterType values");
};
