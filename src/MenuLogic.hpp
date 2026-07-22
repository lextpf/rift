#pragma once

#include <vector>

/**
 * @brief Pure menu navigation primitives shared by Title and Pause overlays.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Free functions over a small POD state struct so tests can exercise them
 * without instantiating Game or touching GLFW. Wrap-around and disabled-item
 * skipping live here; rendering and key dispatch live in @c GameMenus.cpp.
 */
namespace MenuLogic
{

/**
 * @struct ItemList
 * @brief Selectable menu (vertical list with disabled items).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * @c enabled[i] gates whether the cursor can land on item @c i.
 *
 * @pre Every navigation helper requires @c selected in [0, enabled.size()). The
 * helpers keep it in that range but never restore it, and a negative value indexes
 * out of bounds because the C++ remainder operator keeps the sign. A caller that
 * assigns @c selected directly (for example from a saved menu position) must clamp
 * it first, or take it from @ref FirstEnabledIndex.
 */
struct ItemList
{
    /// One flag per menu item; its size is the item count. A false entry is skipped
    /// by @ref NavigateUp / @ref NavigateDown (e.g. a greyed-out "Continue").
    std::vector<bool> enabled;
    /// Index of the highlighted item. The navigation helpers only ever move it to an
    /// enabled index; they never validate a value assigned directly by a caller.
    int selected = 0;
};

/**
 * @brief Index of the first enabled item, or 0 if none.
 *
 * Used to pick a sane default when opening the menu (e.g., land on
 * "New Game" instead of a greyed-out "Continue").
 */
inline int FirstEnabledIndex(const ItemList& list)
{
    for (int i = 0; i < static_cast<int>(list.enabled.size()); ++i)
    {
        if (list.enabled[i])
        {
            return i;
        }
    }
    return 0;
}

namespace detail
{
inline bool AnyEnabledOtherThan(const ItemList& list, int idx)
{
    for (int i = 0; i < static_cast<int>(list.enabled.size()); ++i)
    {
        if (i != idx && list.enabled[i])
        {
            return true;
        }
    }
    return false;
}
}  // namespace detail

/**
 * @brief Move the cursor down one position, wrapping and skipping disabled.
 *
 * No-op when @c selected is the only enabled item.
 */
inline void NavigateDown(ItemList& list)
{
    const int n = static_cast<int>(list.enabled.size());
    if (n == 0 || !detail::AnyEnabledOtherThan(list, list.selected))
    {
        return;
    }
    int next = list.selected;
    for (int step = 0; step < n; ++step)
    {
        next = (next + 1) % n;
        if (list.enabled[next])
        {
            list.selected = next;
            return;
        }
    }
}

/**
 * @brief Move the cursor up one position, wrapping and skipping disabled.
 *
 * No-op when @c selected is the only enabled item.
 */
inline void NavigateUp(ItemList& list)
{
    const int n = static_cast<int>(list.enabled.size());
    if (n == 0 || !detail::AnyEnabledOtherThan(list, list.selected))
    {
        return;
    }
    int next = list.selected;
    for (int step = 0; step < n; ++step)
    {
        next = (next - 1 + n) % n;
        if (list.enabled[next])
        {
            list.selected = next;
            return;
        }
    }
}

/**
 * @enum ConfirmChoice
 * @brief The two answers to a confirmation prompt (e.g., "overwrite save?").
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 */
enum class ConfirmChoice : uint8_t
{
    /// Dismiss the prompt without acting. The default, so a stray confirm keypress
    /// cannot destroy anything.
    Cancel,
    Confirm  ///< Carry out the destructive action.
};

/**
 * @struct ConfirmPrompt
 * @brief State of one two-option confirmation prompt.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 */
struct ConfirmPrompt
{
    /// Highlighted answer. Left/right saturate instead of wrapping, so holding a
    /// direction cannot cycle back onto Confirm.
    ConfirmChoice selected = ConfirmChoice::Cancel;
};

/// Move selection toward "Confirm". Saturates (no wrap) on the right edge.
inline void ConfirmRight(ConfirmPrompt& p)
{
    p.selected = ConfirmChoice::Confirm;
}

/// Move selection toward "Cancel". Saturates (no wrap) on the left edge.
inline void ConfirmLeft(ConfirmPrompt& p)
{
    p.selected = ConfirmChoice::Cancel;
}

}  // namespace MenuLogic
