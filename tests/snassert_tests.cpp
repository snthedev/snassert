#include <gtest/gtest.h>

#include <snassert/snassert.hpp>

#include <iostream>
#include <sstream>
#include <string>

// snassert is fully compiled out under NDEBUG, so the failure-path tests
// only make sense in Debug builds. Release builds compile this TU cleanly
// and simply run zero tests.
#ifndef NDEBUG

namespace
{
    using sn::assert::settings_t;
    namespace d = sn::assert::details;

    int g_breaks = 0;

    void countingBreak()
    {
        ++g_breaks;
    }

    std::stringstream g_captured;
    std::streambuf* g_oldBuf = nullptr;

    class SnassertTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            g_breaks = 0;
            g_captured.str(std::string());
            g_captured.clear();
            d::debugBreaker::setBreakFn(&countingBreak);
            d::setColorPolicy(d::color_policy_t::never);
            g_oldBuf = std::cerr.rdbuf(g_captured.rdbuf());
        }

        void TearDown() override
        {
            std::cerr.rdbuf(g_oldBuf);
            g_oldBuf = nullptr;
        }

        static std::string output()
        {
            return g_captured.str();
        }
    };

    TEST_F(SnassertTest, PassingExprDoesNothing)
    {
        int x = 42;
        snassert(x == 42);
        EXPECT_EQ(g_breaks, 0);
        EXPECT_TRUE(output().empty());

        snassert(x == 42, "must hold");
        EXPECT_EQ(g_breaks, 0);
        EXPECT_TRUE(output().empty());
    }

    TEST_F(SnassertTest, OneArgFailurePrintsExpressionText)
    {
        int x = 41;
        snassert(x == 42);
        EXPECT_EQ(g_breaks, 1);

        const std::string out = output();
        EXPECT_NE(out.find("<Assertion failed>"), std::string::npos);
        EXPECT_NE(out.find("x == 42"), std::string::npos);          // #expr is printed
        EXPECT_NE(out.find("snassert_tests.cpp"), std::string::npos);
        EXPECT_EQ(out.find("- "), std::string::npos);               // no msg section
        EXPECT_EQ(out.find("+ "), std::string::npos);               // no tips section
    }

    TEST_F(SnassertTest, TwoArgsMessageOnly)
    {
        snassert(1 > 2, "one must be greater than two");
        EXPECT_EQ(g_breaks, 1);

        const std::string out = output();
        EXPECT_NE(out.find("1 > 2"), std::string::npos);
        EXPECT_NE(out.find("- one must be greater than two"), std::string::npos);
        EXPECT_EQ(out.find("+ "), std::string::npos);
    }

    TEST_F(SnassertTest, ThreeArgsMessageAndTips)
    {
        snassert(false, "msg line", "tip line");
        EXPECT_EQ(g_breaks, 1);

        const std::string out = output();
        EXPECT_NE(out.find("false"), std::string::npos);
        EXPECT_NE(out.find("- msg line"), std::string::npos);
        EXPECT_NE(out.find("+ tip line"), std::string::npos);
    }

    TEST_F(SnassertTest, SettingsMsgAndTips)
    {
        snassert(false, (settings_t{ .msg = "settings msg", .tips = "settings tips" }));
        EXPECT_EQ(g_breaks, 1);

        const std::string out = output();
        EXPECT_NE(out.find("- settings msg"), std::string::npos);
        EXPECT_NE(out.find("+ settings tips"), std::string::npos);
    }

    TEST_F(SnassertTest, SettingsMsgOnly)
    {
        snassert(false, (settings_t{ .msg = "only msg" }));
        EXPECT_EQ(g_breaks, 1);

        const std::string out = output();
        EXPECT_NE(out.find("- only msg"), std::string::npos);
        EXPECT_EQ(out.find("+ "), std::string::npos);
    }

    TEST_F(SnassertTest, SettingsTipsOnly)
    {
        snassert(false, (settings_t{ .tips = "only tips" }));
        EXPECT_EQ(g_breaks, 1);

        const std::string out = output();
        EXPECT_NE(out.find("+ only tips"), std::string::npos);
        EXPECT_EQ(out.find("- "), std::string::npos);
    }

    TEST_F(SnassertTest, MultilineMessageGetsIndent)
    {
        snassert(false, "line1\nline2\r\nline3\n");
        const std::string out = output();

        // indentation is inserted after each line break,
        // original break sequences are preserved as-is
        EXPECT_NE(out.find("- line1\n  line2\r\n  line3"), std::string::npos);
    }

    TEST_F(SnassertTest, ExprEvaluatedExactlyOnceOnFailure)
    {
        struct Counter
        {
            int n = 0;
            bool check() { ++n; return false; }
        };

        Counter c;
        snassert(c.check(), "boom");
        EXPECT_EQ(c.n, 1);
    }

    TEST_F(SnassertTest, ExprEvaluatedExactlyOnceOnSuccess)
    {
        struct Counter
        {
            int n = 0;
            bool check() { ++n; return true; }
        };

        Counter c;
        snassert(c.check(), "boom");
        EXPECT_EQ(c.n, 1);
        EXPECT_EQ(g_breaks, 0);
    }

    TEST_F(SnassertTest, ColorAlwaysEmitsEscapeSequences)
    {
        d::setColorPolicy(d::color_policy_t::always);
        snassert(false, "colored msg");

        const std::string out = output();
        EXPECT_NE(out.find("\x1b["), std::string::npos);
        EXPECT_NE(out.find("colored msg"), std::string::npos);
    }

    TEST_F(SnassertTest, BreakFnCanBeReplacedAndRestored)
    {
        static int custom_breaks = 0;
        custom_breaks = 0;

        auto custom = [] { ++custom_breaks; };
        d::debugBreaker::setBreakFn(custom);

        snassert(false);
        EXPECT_EQ(custom_breaks, 1);
        EXPECT_EQ(g_breaks, 0); // default counter not touched

        // nullptr restores the default; do NOT call it here (it would __debugbreak).
        d::debugBreaker::setBreakFn(nullptr);

        // restore the counting fn for TearDown consistency
        d::debugBreaker::setBreakFn(&countingBreak);
    }

    TEST_F(SnassertTest, CommaInParenthesizedExprIsAccepted)
    {
        auto f = [](int a, int b) { return a + b; };
        snassert((f(2, 3) == 5), "sum check");

        EXPECT_EQ(g_breaks, 0);
        EXPECT_TRUE(output().empty());
    }
}

#endif // NDEBUG

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
