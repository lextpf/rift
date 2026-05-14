// Tests for menu navigation logic used by the title screen and pause overlay.
// Pure data tests - no GL/Vulkan context, no GLFW window.
//
// Verifies cursor wrap-around, disabled-item skipping, and the
// confirm-overwrite prompt's two-state toggle.

#include <gtest/gtest.h>

#include "../src/MenuLogic.hpp"

namespace
{

MenuLogic::ItemList MakeAllEnabled(int count, int initialSelection = 0)
{
    MenuLogic::ItemList list;
    list.enabled.assign(count, true);
    list.selected = initialSelection;
    return list;
}

}  // namespace

// ----- NavigateDown / NavigateUp basics -----

TEST(MenuLogic, NavigateDownAdvancesByOne)
{
    auto list = MakeAllEnabled(4);
    MenuLogic::NavigateDown(list);
    EXPECT_EQ(list.selected, 1);
}

TEST(MenuLogic, NavigateUpRetreatsByOne)
{
    auto list = MakeAllEnabled(4, /*initial=*/2);
    MenuLogic::NavigateUp(list);
    EXPECT_EQ(list.selected, 1);
}

TEST(MenuLogic, NavigateDownWrapsFromLastToFirst)
{
    auto list = MakeAllEnabled(4, /*initial=*/3);
    MenuLogic::NavigateDown(list);
    EXPECT_EQ(list.selected, 0);
}

TEST(MenuLogic, NavigateUpWrapsFromFirstToLast)
{
    auto list = MakeAllEnabled(4, /*initial=*/0);
    MenuLogic::NavigateUp(list);
    EXPECT_EQ(list.selected, 3);
}

// ----- Disabled-item skipping -----

TEST(MenuLogic, NavigateDownSkipsDisabled)
{
    // Layout: [New, Continue(disabled), Settings(disabled), Quit]
    MenuLogic::ItemList list;
    list.enabled = {true, false, false, true};
    list.selected = 0;

    MenuLogic::NavigateDown(list);
    EXPECT_EQ(list.selected, 3);  // skips both disabled items
}

TEST(MenuLogic, NavigateUpSkipsDisabled)
{
    MenuLogic::ItemList list;
    list.enabled = {true, false, false, true};
    list.selected = 3;

    MenuLogic::NavigateUp(list);
    EXPECT_EQ(list.selected, 0);
}

TEST(MenuLogic, NavigateDownWrapsAcrossDisabled)
{
    // Layout: [New, Continue(disabled), Settings(disabled), Quit]
    MenuLogic::ItemList list;
    list.enabled = {true, false, false, true};
    list.selected = 3;

    MenuLogic::NavigateDown(list);
    EXPECT_EQ(list.selected, 0);  // wraps past disabled items
}

TEST(MenuLogic, NavigateUpWrapsAcrossDisabled)
{
    MenuLogic::ItemList list;
    list.enabled = {true, false, false, true};
    list.selected = 0;

    MenuLogic::NavigateUp(list);
    EXPECT_EQ(list.selected, 3);
}

TEST(MenuLogic, NavigateNoOpWhenAllDisabledExceptCurrent)
{
    MenuLogic::ItemList list;
    list.enabled = {false, true, false, false};
    list.selected = 1;

    MenuLogic::NavigateDown(list);
    EXPECT_EQ(list.selected, 1);
    MenuLogic::NavigateUp(list);
    EXPECT_EQ(list.selected, 1);
}

// ----- Initial selection helper -----

TEST(MenuLogic, FirstEnabledIndexFindsFirstEnabled)
{
    MenuLogic::ItemList list;
    list.enabled = {false, false, true, true};
    EXPECT_EQ(MenuLogic::FirstEnabledIndex(list), 2);
}

TEST(MenuLogic, FirstEnabledIndexReturnsZeroWhenAllDisabled)
{
    MenuLogic::ItemList list;
    list.enabled = {false, false, false};
    EXPECT_EQ(MenuLogic::FirstEnabledIndex(list), 0);
}

// ----- Confirm prompt toggle (2-option, no wrap) -----

TEST(MenuPrompt, DefaultsToCancel)
{
    MenuLogic::ConfirmPrompt p;
    EXPECT_EQ(p.selected, MenuLogic::ConfirmChoice::Cancel);
}

TEST(MenuPrompt, RightTogglesToConfirm)
{
    MenuLogic::ConfirmPrompt p;
    MenuLogic::ConfirmRight(p);
    EXPECT_EQ(p.selected, MenuLogic::ConfirmChoice::Confirm);
}

TEST(MenuPrompt, RightFromConfirmStaysOnConfirm)
{
    MenuLogic::ConfirmPrompt p;
    p.selected = MenuLogic::ConfirmChoice::Confirm;
    MenuLogic::ConfirmRight(p);
    EXPECT_EQ(p.selected, MenuLogic::ConfirmChoice::Confirm);
}

TEST(MenuPrompt, LeftFromConfirmReturnsToCancel)
{
    MenuLogic::ConfirmPrompt p;
    p.selected = MenuLogic::ConfirmChoice::Confirm;
    MenuLogic::ConfirmLeft(p);
    EXPECT_EQ(p.selected, MenuLogic::ConfirmChoice::Cancel);
}

TEST(MenuPrompt, LeftFromCancelStaysOnCancel)
{
    MenuLogic::ConfirmPrompt p;
    MenuLogic::ConfirmLeft(p);
    EXPECT_EQ(p.selected, MenuLogic::ConfirmChoice::Cancel);
}
