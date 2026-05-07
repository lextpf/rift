// Tests for ConsoleBuffer: the developer-console scrollback ring, input line,
// cursor, history, and scroll-offset state. Pure data layer - no GLFW or
// renderer involvement.

#include <gtest/gtest.h>

#include "../src/Console.h"

#include <string>

namespace
{
/// Drive the buffer with a literal string, one codepoint at a time.
void TypeString(ConsoleBuffer& buf, std::string_view text)
{
    for (char c : text)
    {
        buf.OnChar(static_cast<std::uint32_t>(c));
    }
}
}  // namespace

TEST(ConsoleBufferTests, PrintAppendsLine)
{
    ConsoleBuffer buf;
    buf.Print("hello");
    ASSERT_EQ(buf.Lines().size(), 1u);
    EXPECT_EQ(buf.Lines().front().text, "hello");
}

TEST(ConsoleBufferTests, RingBufferDropsOldestPastCapacity)
{
    ConsoleBuffer buf;
    for (std::size_t i = 0; i < ConsoleBuffer::MAX_LINES + 5; ++i)
    {
        buf.Print("line " + std::to_string(i));
    }
    EXPECT_EQ(buf.Lines().size(), ConsoleBuffer::MAX_LINES);
    // Oldest 5 should have been evicted; the front line should be "line 5".
    EXPECT_EQ(buf.Lines().front().text, "line 5");
    EXPECT_EQ(buf.Lines().back().text, "line " + std::to_string(ConsoleBuffer::MAX_LINES + 4));
}

TEST(ConsoleBufferTests, ClearRemovesScrollbackButNotInputOrHistory)
{
    ConsoleBuffer buf;
    buf.Print("a");
    buf.Print("b");
    TypeString(buf, "hello");
    buf.RecordHistory("prev");

    buf.Clear();
    EXPECT_TRUE(buf.Lines().empty());
    EXPECT_EQ(buf.Input(), "hello");
    EXPECT_EQ(buf.History().size(), 1u);
}

TEST(ConsoleBufferTests, OnCharInsertsAtCursor)
{
    ConsoleBuffer buf;
    TypeString(buf, "hllo");
    buf.OnHome();
    buf.OnRight();  // cursor between 'h' and 'l'
    buf.OnChar('e');
    EXPECT_EQ(buf.Input(), "hello");
    EXPECT_EQ(buf.CursorPos(), 2u);
}

TEST(ConsoleBufferTests, BackspaceAndDelete)
{
    ConsoleBuffer buf;
    TypeString(buf, "abcd");
    buf.OnLeft();       // cursor between 'c' and 'd'
    buf.OnBackspace();  // erase 'c'
    EXPECT_EQ(buf.Input(), "abd");
    EXPECT_EQ(buf.CursorPos(), 2u);
    buf.OnDelete();  // erase 'd'
    EXPECT_EQ(buf.Input(), "ab");
    EXPECT_EQ(buf.CursorPos(), 2u);
}

TEST(ConsoleBufferTests, CursorClampsAtBoundaries)
{
    ConsoleBuffer buf;
    TypeString(buf, "ab");
    buf.OnLeft();
    buf.OnLeft();
    buf.OnLeft();  // clamp at 0
    EXPECT_EQ(buf.CursorPos(), 0u);
    buf.OnRight();
    buf.OnRight();
    buf.OnRight();  // clamp at length
    EXPECT_EQ(buf.CursorPos(), 2u);
    buf.OnHome();
    EXPECT_EQ(buf.CursorPos(), 0u);
    buf.OnEnd();
    EXPECT_EQ(buf.CursorPos(), 2u);
}

TEST(ConsoleBufferTests, OnEnterReturnsAndClears)
{
    ConsoleBuffer buf;
    TypeString(buf, "submit me");
    EXPECT_EQ(buf.OnEnter(), "submit me");
    EXPECT_EQ(buf.Input(), "");
    EXPECT_EQ(buf.CursorPos(), 0u);
}

TEST(ConsoleBufferTests, NonPrintableCodepointsIgnored)
{
    ConsoleBuffer buf;
    buf.OnChar(0x07);  // bell
    buf.OnChar(0xA0);  // non-breaking space (above ASCII printable)
    EXPECT_EQ(buf.Input(), "");
}

TEST(ConsoleBufferTests, HistoryPrevWalksBackwards)
{
    ConsoleBuffer buf;
    buf.RecordHistory("first");
    buf.RecordHistory("second");
    buf.RecordHistory("third");

    auto a = buf.HistoryPrev();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, "third");

    auto b = buf.HistoryPrev();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(*b, "second");

    auto c = buf.HistoryPrev();
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(*c, "first");

    // Past the oldest entry is a stable nullopt.
    EXPECT_FALSE(buf.HistoryPrev().has_value());
    EXPECT_FALSE(buf.HistoryPrev().has_value());
}

TEST(ConsoleBufferTests, HistoryNextStepsForwardAndExitsToEmpty)
{
    ConsoleBuffer buf;
    buf.RecordHistory("alpha");
    buf.RecordHistory("beta");

    EXPECT_FALSE(buf.HistoryNext().has_value());  // not navigating yet

    (void)buf.HistoryPrev();  // -> beta
    (void)buf.HistoryPrev();  // -> alpha

    auto a = buf.HistoryNext();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, "beta");

    auto b = buf.HistoryNext();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(*b, "");  // stepped past newest -> empty prompt

    EXPECT_FALSE(buf.HistoryNext().has_value());  // no longer navigating
}

TEST(ConsoleBufferTests, HistorySkipsEmptyAndConsecutiveDuplicates)
{
    ConsoleBuffer buf;
    buf.RecordHistory("");
    buf.RecordHistory("a");
    buf.RecordHistory("a");
    buf.RecordHistory("b");
    buf.RecordHistory("b");
    buf.RecordHistory("a");
    EXPECT_EQ(buf.History().size(), 3u);
    EXPECT_EQ(buf.History()[0], "a");
    EXPECT_EQ(buf.History()[1], "b");
    EXPECT_EQ(buf.History()[2], "a");
}

TEST(ConsoleBufferTests, TypingResetsHistoryIndex)
{
    ConsoleBuffer buf;
    buf.RecordHistory("one");
    (void)buf.HistoryPrev();
    EXPECT_TRUE(buf.HistoryIndex().has_value());
    buf.OnChar('x');
    EXPECT_FALSE(buf.HistoryIndex().has_value());
}

TEST(ConsoleBufferTests, ScrollOffsetClampsToBufferSize)
{
    ConsoleBuffer buf;
    for (int i = 0; i < 5; ++i)
    {
        buf.Print("line");
    }
    buf.Scroll(-10);
    EXPECT_EQ(buf.ScrollOffset(), 0);
    buf.Scroll(100);
    EXPECT_EQ(buf.ScrollOffset(), 5);  // clamped to lines.size()
}

TEST(ConsoleBufferTests, NewPrintAutoSnapsScrollToBottom)
{
    ConsoleBuffer buf;
    for (int i = 0; i < 3; ++i)
    {
        buf.Print("old");
    }
    buf.Scroll(2);
    EXPECT_EQ(buf.ScrollOffset(), 2);
    buf.Print("new");
    EXPECT_EQ(buf.ScrollOffset(), 0);  // snapped on new output
}
