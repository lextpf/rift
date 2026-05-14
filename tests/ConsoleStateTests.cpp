// Tests for the developer console's visibility state machine. Exercises the
// pure transition function `NextConsoleState` so the cycle Closed -> Half ->
// Full -> Closed can be validated without constructing a Console + Game pair
// (which would require a graphics context per the test-suite constraints).

#include <gtest/gtest.h>

#include "../src/Console.hpp"

TEST(ConsoleStateTests, ClosedAdvancesToHalf)
{
    EXPECT_EQ(NextConsoleState(Console::State::Closed), Console::State::Half);
}

TEST(ConsoleStateTests, HalfAdvancesToFull)
{
    EXPECT_EQ(NextConsoleState(Console::State::Half), Console::State::Full);
}

TEST(ConsoleStateTests, FullAdvancesToClosed)
{
    EXPECT_EQ(NextConsoleState(Console::State::Full), Console::State::Closed);
}

TEST(ConsoleStateTests, ThreeStepCycleReturnsToStart)
{
    Console::State s = Console::State::Closed;
    s = NextConsoleState(s);
    s = NextConsoleState(s);
    s = NextConsoleState(s);
    EXPECT_EQ(s, Console::State::Closed);
}

TEST(ConsoleStateTests, FunctionIsConstexpr)
{
    // Compile-time validation: if NextConsoleState ever loses constexpr,
    // these constant_expressions stop compiling.
    constexpr auto a = NextConsoleState(Console::State::Closed);
    constexpr auto b = NextConsoleState(Console::State::Half);
    constexpr auto c = NextConsoleState(Console::State::Full);
    static_assert(a == Console::State::Half);
    static_assert(b == Console::State::Full);
    static_assert(c == Console::State::Closed);
}
