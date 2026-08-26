# snassert

A drop-in replacement for the standard C++ `<cassert>` macro with **human-friendly failure reports**: a custom message, actionable tips, colored terminal output — and full testability.

Built as a single header-only file. Requires **C++23** and **MSVC (Visual Studio 2026, toolset v145)** on Windows.

Status: MVP complete, **13 tests green** (Debug, x64); Release compiles clean
(assertions compiled out by design).

---

## Why not `<cassert>`?

| Aspect | `<cassert>` | **snassert** |
|---|---|---|
| Custom message on failure | ❌ No — you only get the expression text, file and line | ✅ Free-form message **and** a separate "tips" section |
| Failure report layout | One flat line, hard to scan in long logs | Structured multi-line report: header, location, expression, message, tips |
| Colored output | ❌ Plain text only | ✅ True-color ANSI report (auto-detected console support, graceful fallback) |
| Runtime color control | ❌ N/A | ✅ `automatic` / `always` / `never` policies + `SNASSERT_NO_COLOR` compile-time switch |
| Customizing what happens on failure | ❌ Hardwired into the CRT (debugger break / dialog / abort) | ✅ Replaceable break handler: log it, throw, count — anything |
| Unit-testability | ❌ Practically impossible: a failed `assert` kills the test process | ✅ First-class: swap the break handler, capture `std::cerr`, assert on the produced report |
| Expression evaluated once | ✅ Yes | ✅ Yes |
| Compiled out in release builds | ✅ With `NDEBUG` | ✅ With `NDEBUG` (expression is *not* evaluated at all, same semantics) |
| Dependencies | None | None (standard library only; no `<windows.h>` pollution) |

In short: `assert` tells you *that* something broke. `snassert` tells you *what broke*, *why it matters* (`msg`) and *what to do about it* (`tips`) — without stopping you from automating or testing any part of that flow.

### Example

```cpp
#include <snassert/snassert.hpp>

void resize(std::span<const int> data, size_t new_size)
{
    // 1 argument: expression only
    snassert(new_size > 0);

    // 2 arguments: expression + message
    snassert(new_size <= kMaxSize, "requested size exceeds the hard limit");

    // 3 arguments: expression + message + tips
    snassert(!data.empty(), "cannot shrink an empty buffer",
                             "check whether the producer ran at all");
}
```

On failure (Debug build) the following report goes to `std::cerr`:

```
<Assertion failed>
* "src/buffer.cpp":42
  expr: !data.empty()
- cannot shrink an empty buffer
+ check whether the producer ran at all
```

In a color-capable terminal every section is highlighted (true-color ANSI). When stderr is redirected to a file or CI log, colors are disabled automatically — the report stays clean plain text.

## Usage

```cpp
#include <snassert/snassert.hpp>
```

That's it — the library is header-only.

### Argument forms

```cpp
snassert(expr);                                        // expression only
snassert(expr, "message");                             // + message
snassert(expr, "message", "tips");                     // + tips
snassert(expr, (sn::assert::settings_t{
                    .msg  = "message",
                    .tips = "tips",
                }));                                   // settings aggregate
```

> **Note:** the preprocessor splits macro arguments on top-level commas, so braced initializers containing commas must be wrapped in parentheses — `(settings_t{ ... })`. This is the same classic limitation as passing comma expressions to the standard `assert`.

### Release builds

With `NDEBUG` defined the macro expands to `((void)(0))`:

- the condition is **not evaluated** (no side effects, same as the standard `assert`);
- messages cost nothing;
- the public API types (`sn::assert::settings_t`, `sn::assert::details::*`) remain available, so code referencing them compiles in both configurations.

## Customization

### Failure action

By default a failure prints the report and calls `__debugbreak()`. The break step is a settable hook:

```cpp
namespace sn::assert::details
{
    class debugBreaker
    {
    public:
        using pfn_doBreak_t = void (*)();

        static void doBreak();                      // invokes the current handler
        static void setBreakFn(pfn_doBreak_t pfn);  // nullptr restores __debugbreak()
    };
}
```

Examples: throw an exception instead of breaking, increment a counter in tests, write to a logging system, or `std::abort()` in CI.

### Colors

```cpp
namespace sn::assert::details
{
    enum class color_policy_t { automatic, always, never };

    void setColorPolicy(color_policy_t policy);
}
```

- `automatic` (default): enable colors only when stderr is a real console and virtual-terminal processing can be enabled (Windows);
- `always`: force ANSI escape codes;
- `never`: plain text.

Compile-time equivalent: define `SNASSERT_NO_COLOR`.

## Testing your own code that uses snassert

The failure path is fully observable, which makes it trivial to unit-test (the library's own suite does exactly this):

```cpp
#include <gtest/gtest.h>
#include <snassert/snassert.hpp>

namespace d = sn::assert::details;

TEST(MyCode, RejectsInvalidInput)
{
    int breaks = 0;
    std::stringstream captured;

    d::debugBreaker::setBreakFn([&breaks] { ++breaks; });   // no debugger, no abort
    auto* old = std::cerr.rdbuf(captured.rdbuf());          // capture the report

    run_code_under_test();

    std::cerr.rdbuf(old);
    EXPECT_EQ(breaks, 1);
    EXPECT_NE(captured.str().find("expr: size <= kMaxSize"), std::string::npos);
}
```

See `tests/snassert_tests.cpp` for a complete example.

The library's own suite covers all argument forms, report contents,
multi-line indentation, expression evaluation count, break-handler replacement
and color policies — **13 tests green** (Debug, x64). Under `NDEBUG` the test
binary builds and runs with zero assertions to exercise, by design.

## Build

**Prerequisites:** Windows, Visual Studio 2022/2026 with the C++ workload
(MSVC v145 toolset), Git.

### One-shot way

```bat
build.bat
```

`build.bat [Debug|Release]` does everything:

1. fetches GoogleTest v1.17.0 into `tests\thirdparty\` on first run (skipped if present);
2. locates MSBuild via `vswhere`;
3. builds the tests (x64);
4. runs them.

`build.bat fetch` only pulls the dependencies without building.

### Manual equivalent

```powershell
git clone --depth 1 --branch v1.17.0 https://github.com/google/googletest.git tests\thirdparty\googletest
msbuild tests\snassert_tests.vcxproj /p:Configuration=Debug /p:Platform=x64
build\snassert_tests_d.exe
```

Or open `snassert.slnx` in Visual Studio and run the `snassert_tests` project.

Test binaries land in `build\`: `snassert_tests_d.exe` (Debug) /
`snassert_tests.exe` (Release). Nothing of the fetched dependencies gets
committed: `tests\thirdparty\` is gitignored.

To use the library in your own project, just add the repository root to your
include paths and `#include <snassert/snassert.hpp>`.

### Project layout

```
snassert/
├── build.bat                      # one-shot fetch + build + test runner
├── snassert.slnx                  # solution (x64 Debug/Release)
├── snassert/
│   ├── snassert.hpp               # the entire library
│   └── snassert.vcxproj           # project for IDE use (header-only lib)
└── tests/
    ├── snassert_tests.cpp         # GoogleTest-based suite
    ├── snassert_tests.vcxproj     # builds tests + gtest-all.cc in one go
    └── thirdparty/                # GoogleTest lands here via build.bat (gitignored)
```

## Known limitations

- MSVC/Windows-first: non-Windows platforms get plain-text output and `std::abort()` as the default break action (compiles, but is not the tested configuration).
- Macro argument counting cannot see through top-level commas — hide them in parentheses (expression *and* braced settings).
- Messages are string literals / `std::string_view`; there is no built-in value formatting (e.g., "expected 42, got 41") yet — see the roadmap.

## Roadmap

- [ ] Optional trimming of `__FILE__` to a project-relative path
- [ ] Additional failure actions besides break (abort / throw / log-only presets)
- [ ] Formatted messages with captured expression values (expected vs. actual)

