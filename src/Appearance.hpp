#pragma once

#include "CharacterType.hpp"

#include <glm/glm.hpp>

/**
 * @struct Appearance
 * @brief Player visual identity: character variant, disguise flag, accent color.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the player's appearance state carved out of PlayerCharacter's loose
 * members. @ref characterType selects the sprite-sheet set; @ref
 * usingCopiedAppearance is set while wearing a copied NPC look (restored via
 * RestoreOriginalAppearance). @ref accentColor is the dialogue accent sampled
 * eagerly from the walking sheet whenever it changes (no lazy/mutable cache).
 *
 * Plain data struct: flat aggregate, usable directly as an ECS component.
 */
struct Appearance
{
    CharacterType characterType{CharacterType::BW1_MALE};  ///< Active character variant.
    bool usingCopiedAppearance{false};  ///< True while wearing a copied NPC appearance.
    glm::vec3 accentColor{0.0f};        ///< Dialogue accent sampled from the walk sheet.
};
