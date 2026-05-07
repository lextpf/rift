#pragma once

#include <vector>

/**
 * @brief Pure menu navigation primitives shared by Title and Pause overlays.
 * @ingroup Core
 *
 * Free functions over a small POD state struct so tests can exercise them
 * without instantiating Game or touching GLFW. Wrap-around and disabled-item
 * skipping live here; rendering and key dispatch live in @c GameMenus.cpp.
 */
namespace MenuLogic
{

/**
 * @brief Selectable menu (vertical list with disabled items).
 *
 * @c enabled[i] gates whether the cursor can land on item @c i.
 * @c selected is always a valid index (or 0 when no item is enabled).
 */
struct ItemList
{
    std::vector<bool> enabled;
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

/// Two-option confirmation prompt (e.g., "overwrite save?").
enum class ConfirmChoice : uint8_t
{
    Cancel,
    Confirm
};

struct ConfirmPrompt
{
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
