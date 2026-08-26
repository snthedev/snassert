#pragma once

/*
snassert - assertion replacement with message & tips.

Usage:
    snassert(expr);
    snassert(expr, "message");
    snassert(expr, "message", "tips");
    snassert(expr, sn::assert::settings_t{ .msg = "message", .tips = "tips" });

On failure: report is written to std::cerr, then
sn::assert::details::debugBreaker::doBreak() is invoked
(default: __debugbreak(); replace via debugBreaker::setBreakFn(pfn),
 pass nullptr to restore the default).

With NDEBUG defined the whole macro degrades to ((void)(0)):
the expression is NOT evaluated.

Compile-time options:
    SNASSERT_NO_COLOR - forcibly disable ANSI colors.

Notes:
    - top-level commas in expr must be hidden in parentheses
      (same as the standard assert);
    - for the same reason wrap braced settings_t initializers
      containing commas into parentheses:
          snassert(cond, (settings_t{ .msg = "m", .tips = "t" }));
    - colors are enabled automatically only when stderr is a console
      and virtual-terminal processing is available (Windows);
      runtime override: sn::assert::details::setColorPolicy(...).
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
// Minimal kernel32/UCRT declarations to avoid pulling in <windows.h>.
extern "C" {
__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long nStdHandle);
__declspec(dllimport) int __stdcall GetConsoleMode(void* hConsoleHandle, unsigned long* lpMode);
__declspec(dllimport) int __stdcall SetConsoleMode(void* hConsoleHandle, unsigned long dwMode);
__declspec(dllimport) int __cdecl _isatty(int fh);
__declspec(dllimport) int __cdecl _fileno(FILE* stream);
}
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace sn::assert
{
    struct settings_t
    {
        std::string_view msg{};
        std::string_view tips{};
    };

    namespace details
    {
        //
        // Failure-breaking customization point.
        //

        class debugBreaker
        {
        public:
            using pfn_doBreak_t = void (*)();

            static void doBreak()
            {
                s_break();
            }

            static void setBreakFn(pfn_doBreak_t pfn_break) noexcept
            {
                s_break = (pfn_break != nullptr) ? pfn_break : &defaultBreak;
            }

        private:
            static void defaultBreak() noexcept
            {
#if defined(_MSC_VER)
                // !!! SEE CONSOLE FOR BREAK DETAILS !!!
                __debugbreak();
#else
                // !!! SEE CONSOLE FOR BREAK DETAILS !!!
                std::abort();
#endif
            }

            static inline pfn_doBreak_t s_break = &defaultBreak;
        };

        //
        // Colors.
        //

        enum class color_policy_t
        {
            automatic, // console + VT available
            always,    // force ANSI codes
            never,     // force plain text
        };

        inline color_policy_t& s_colorPolicy() noexcept
        {
            static color_policy_t policy = color_policy_t::automatic;
            return policy;
        }

        inline void setColorPolicy(color_policy_t policy) noexcept
        {
            s_colorPolicy() = policy;
        }

        inline bool useColor()
        {
            switch (s_colorPolicy())
            {
                case color_policy_t::never:  return false;
                case color_policy_t::always: return true;
                case color_policy_t::automatic: break;
            }

#if defined(SNASSERT_NO_COLOR)
            return false;
#elif defined(_WIN32)
            static const bool enabled = []() noexcept
            {
                if (::_isatty(::_fileno(stderr)) == 0)
                    return false;

                void* const err = ::GetStdHandle(static_cast<unsigned long>(-12L)); /* STD_ERROR_HANDLE */
                if (err == nullptr)
                    return false;

                unsigned long mode = 0;
                if (::GetConsoleMode(err, &mode) == 0)
                    return false;

                constexpr unsigned long kEnableVirtualTerminalProcessing = 0x0004UL;
                if ((mode & kEnableVirtualTerminalProcessing) == 0UL &&
                    ::SetConsoleMode(err, mode | kEnableVirtualTerminalProcessing) == 0)
                    return false;

                return true;
            }();
            return enabled;
#else
            return ::isatty(::fileno(stderr)) != 0;
#endif
        }
    }
}

namespace sn::assert
{
    namespace details
    {
        //
        // Output.
        //

        // Inserts `indent` after every line break (\r\n, \n or lone \r),
        // skipping the position right after the final break.
        inline std::string indentAfterNewlines(std::string_view text, std::string_view indent)
        {
            std::string out;
            out.reserve(text.size() + 32);

            for (std::size_t i = 0u; i < text.size(); )
            {
                const char ch = text[i];
                out.push_back(ch);
                ++i;

                bool broke_line = false;
                if (ch == '\r')
                {
                    if (i < text.size() && text[i] == '\n')
                    {
                        out.push_back('\n');
                        ++i;
                    }
                    broke_line = true;
                }
                else if (ch == '\n')
                {
                    broke_line = true;
                }

                if (broke_line && i < text.size())
                    out += indent;
            }

            return out;
        }

        inline std::string fgEsc(std::uint8_t r, std::uint8_t g, std::uint8_t b)
        {
            return std::format("\x1b[38;2;{};{};{}m", r, g, b);
        }

        inline std::string bgEsc(std::uint8_t r, std::uint8_t g, std::uint8_t b)
        {
            return std::format("\x1b[48;2;{};{};{}m", r, g, b);
        }

        inline void printReport(
            std::string_view expr_text,
            const char* file,
            unsigned line,
            std::string_view msg,
            std::string_view tips
        )
        {
            if (useColor())
            {
                const std::string reset = "\x1b[0m";
                const std::string head = fgEsc(255, 255, 255) + bgEsc(0, 0, 170);

                std::cerr
                    << head << "<Assertion failed>" << reset << '\n'

                    << "* \""
                    << fgEsc(153, 199, 148) << file << reset
                    << fgEsc(96, 180, 180) << '"' << reset
                    << ':'
                    << fgEsc(249, 174, 87) << line << reset << '\n'

                    << "  expr: "
                    << fgEsc(120, 160, 255) << expr_text << reset << '\n';

                if (!msg.empty())
                    std::cerr
                        << "- "
                        << fgEsc(255, 150, 150) << indentAfterNewlines(msg, "  ") << reset << '\n';

                if (!tips.empty())
                    std::cerr
                        << "+ "
                        << fgEsc(150, 255, 150) << indentAfterNewlines(tips, "  ") << reset << '\n';
            }
            else
            {
                std::cerr
                    << "<Assertion failed>\n"

                    << "* \"" << file << "\":" << line << '\n'
                    << "  expr: " << expr_text << '\n';

                if (!msg.empty())
                    std::cerr << "- " << indentAfterNewlines(msg, "  ") << '\n';

                if (!tips.empty())
                    std::cerr << "+ " << indentAfterNewlines(tips, "  ") << '\n';
            }

            std::cerr.flush();
        }

        //
        // Entry points (overload set for the macro dispatch).
        //

        inline void fail(std::string_view expr_text, const char* file, unsigned line)
        {
            printReport(expr_text, file, line, {}, {});
            debugBreaker::doBreak();
        }

        inline void fail(std::string_view expr_text, const char* file, unsigned line, std::string_view msg)
        {
            printReport(expr_text, file, line, msg, {});
            debugBreaker::doBreak();
        }

        inline void fail(std::string_view expr_text, const char* file, unsigned line, ::sn::assert::settings_t settings)
        {
            printReport(expr_text, file, line, settings.msg, settings.tips);
            debugBreaker::doBreak();
        }

        inline void fail(std::string_view expr_text, const char* file, unsigned line, std::string_view msg, std::string_view tips)
        {
            printReport(expr_text, file, line, msg, tips);
            debugBreaker::doBreak();
        }
    }
}

//
// Macro dispatch by argument count.
//

#define snassert_DETAIL_CAT(a, b)   snassert_DETAIL_CAT_I(a, b)
#define snassert_DETAIL_CAT_I(a, b) a##b
#define snassert_DETAIL_EXPAND(x)   x
#define snassert_DETAIL_DISPATCH(_1, _2, _3, NAME, ...) NAME

#define snassert_1(expr) \
    ((void)((!!(expr)) || (::sn::assert::details::fail(#expr, __FILE__, static_cast<unsigned>(__LINE__)), 0)))

#define snassert_2(expr, msg_or_settings) \
    ((void)((!!(expr)) || (::sn::assert::details::fail(#expr, __FILE__, static_cast<unsigned>(__LINE__), (msg_or_settings)), 0)))

#define snassert_3(expr, msg, tips) \
    ((void)((!!(expr)) || (::sn::assert::details::fail(#expr, __FILE__, static_cast<unsigned>(__LINE__), (msg), (tips)), 0)))

#ifndef NDEBUG

#define snassert(...) \
    snassert_DETAIL_EXPAND(snassert_DETAIL_DISPATCH(__VA_ARGS__, snassert_3, snassert_2, snassert_1)(__VA_ARGS__))

#else // NDEBUG

#define snassert(...) ((void)(0))

#endif // NDEBUG

