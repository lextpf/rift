#pragma once

#include "CharacterType.hpp"

#include <glm/glm.hpp>

/**
 * @struct Appearance
 * @brief Player visual identity: character variant, disguise flag, accent color.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * @ref characterType selects the sprite sheet set. @ref usingCopiedAppearance stays true while the
 * player wears a copied NPC look, until RestoreOriginalAppearance puts the original back. A
 * failed restore leaves the flag set: RestoreOriginalAppearance clears it only when the
 * character switch succeeds, so the disguise sheet can still be committed with the flag true.
 * @ref accentColor is sampled from the walking sheet every time the appearance changes, so there
 * is no lazy or mutable cache to invalidate.
 */
struct Appearance
{
    CharacterType characterType{CharacterType::BW1_MALE};  ///< Active character variant.
    bool usingCopiedAppearance{false};  ///< True while wearing a copied NPC appearance.
    glm::vec3 accentColor{0.0f};        ///< Dialogue accent sampled from the walking sheet.
};
