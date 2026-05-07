// Tests for Console::Tokenize: ASCII whitespace splitting used by command
// parsing. No GLFW or renderer involvement.

#include <gtest/gtest.h>

#include "../src/Console.h"

#include <string>

TEST(ConsoleParseTests, EmptyInputProducesNoTokens)
{
    EXPECT_TRUE(Console::Tokenize("").empty());
    EXPECT_TRUE(Console::Tokenize("   ").empty());
    EXPECT_TRUE(Console::Tokenize("\t\t").empty());
}

TEST(ConsoleParseTests, SplitsOnSpacesAndTabs)
{
    auto t = Console::Tokenize("flag.set unlocked true");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0], "flag.set");
    EXPECT_EQ(t[1], "unlocked");
    EXPECT_EQ(t[2], "true");
}

TEST(ConsoleParseTests, CollapsesWhitespaceRuns)
{
    auto t = Console::Tokenize("  teleport   10  20  ");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0], "teleport");
    EXPECT_EQ(t[1], "10");
    EXPECT_EQ(t[2], "20");
}

TEST(ConsoleParseTests, MixedSpaceAndTabDelimiters)
{
    auto t = Console::Tokenize("a\tb \tc");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0], "a");
    EXPECT_EQ(t[1], "b");
    EXPECT_EQ(t[2], "c");
}

TEST(ConsoleParseTests, SingleTokenReturnsSingleView)
{
    auto t = Console::Tokenize("help");
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0], "help");
}

TEST(ConsoleParseTests, ViewsPointIntoOriginalBuffer)
{
    // Sanity: the returned views are not separately allocated copies. They
    // are slices of the input. (Caller-of-Tokenize must keep input alive.)
    std::string source = "alpha beta";
    auto t = Console::Tokenize(source);
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[0].data(), source.data());
    EXPECT_EQ(t[1].data(), source.data() + 6);
}
