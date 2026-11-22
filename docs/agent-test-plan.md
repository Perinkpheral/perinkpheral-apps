# Test plan for an unattended agent: perinkpheral-apps

This plan is for a coding agent that works alone for one or two days. Each task is small
and stands alone. Do the tasks in order. Finish one task, write its report, then start the
next.

## Rules

- Do not change application code under `apps/`, the packages under `catalog/` or the
  pictures under `tests/`. You may add new files under `fuzz/` and new scenario folders
  under `tests-agent/` only.
- Do not commit, push, tag or open pull requests.
- Do not contact any device, router or real service. Use simulator mocks only. Never put
  a real address, token or name into a mock.
- Do not fix a bug you find. Report it.
- After each task, append its report to `REPORT.md` in the report folder you were given.
  If no folder was given, use `fuzz/REPORT.md`.

## Report format, for every finding

```
### <task number>.<finding number> <app>: <one-line title>
File: <path>:<line>
Input: <the smallest input, as a file under fuzz/crashes/ or a scenario folder>
What happens: <crash / sanitizer message / wrong picture / hang>
Expected: <what the app or the platform contract promises>
How to reproduce: <one command>
```

If a task finds nothing, write `### <task number> nothing found` with the run time.

## Install the toolchain first

There is no device. Everything runs on this computer, in the simulator. Install these
before any task, and check each with the command after it:

- Git: `git --version`.
- Java 21 or later (Temurin is fine): `java -version`.
- A C compiler. For fuzzing you need clang with libFuzzer: on Linux the distribution's
  `clang` and `llvm`; on macOS Homebrew's `llvm` (Apple's clang has no libFuzzer), used as
  `$(brew --prefix llvm)/bin/clang`. Check: `clang --version`.
- wasi-sdk 34 from https://github.com/WebAssembly/wasi-sdk/releases (the archive for this
  computer's system and processor). Unpack it and `export WASI_SDK_PATH=<its folder>`.
  Check: `$WASI_SDK_PATH/bin/clang --version`.
- Python 3.12 or later: `python3 --version`.
- Node.js 22 and npm: `node --version`.
- ffmpeg: `ffmpeg -version`.
- Optional, for checks that match the Linux CI: Docker with the `gcc:14` image.

ESP-IDF is not needed: nothing here builds or flashes device firmware.

Get the repositories side by side in one folder, from https://github.com/Perinkpheral:
`perinkpheral-os`, `perinkpheral-apps`, `perinkpheral-hacs`, `perinkpheral-dev-portal`,
`perinkpheral-clion-plugin`. The tools find each other as siblings.

Then build the tools and the simulator once and run an application in it:

```sh
cd perinkpheral-os
tools/perinkpheral                        # builds the Kotlin tools on first use, prints the commands
make -C firmware/test/host test           # host tests; needs WASI_SDK_PATH
tools/perinkpheral sim-test firmware/test/sim
tools/perinkpheral sim sdk/apps/hello     # the simulator with one application; stop it with Ctrl-C
```

All four must work before Task 1. If one fails, write the exact error into the report and
stop: do not change the repository to make it pass.

## Setup

The platform is the firmware repository, usually next to this one
(`../perinkpheral-os`). Read its `CLAUDE.md` sections "Perinkpheral OS API" and
"Execution model". Then:

```sh
export PERINKPHERAL=../perinkpheral-os WASI_SDK_PATH=<wasi-sdk directory>
$PERINKPHERAL/tools/perinkpheral app-check apps
$PERINKPHERAL/tools/perinkpheral sim-test tests
```

Both must pass before you start. Each application's own tests are in `apps/<app>/test/`;
their compiler flags are in the firmware repository's `tools/core` (`AppCheck.kt`,
`FLAGS`). Applications use `sdk/lib` of the firmware, not the C library.

## Task 1. Fuzz the calendar's iCal parser

Target: `apps/calendar/ics.c` (`ics_begin`, `ics_feed`, `ics_end`, `ics_opening_feed`).
It reads untrusted calendar files of up to several megabytes, in 1 KB pieces.

1. Write `fuzz/fuzz_ics.c` with libFuzzer (`clang -fsanitize=fuzzer,address,undefined`,
   the same include paths as `apps/calendar/test/test_ics.c`). Feed the input in random
   pieces of 1 to 1024 bytes. Use a fixed `from`/`until` window.
2. Seeds: the iCal bodies in `tests/calendar*/` mocks and in `apps/calendar/test/`.
   Add hand-made seeds with: folded lines, CRLF and LF, very long lines, RRULE with
   COUNT/UNTIL/BYDAY, EXDATE, VTIMEZONE with DST, all-day events, X-WR-CALNAME with UTF-8.
3. Check also: the same file in one piece and in 1-byte pieces gives the same events.
4. Run 6 hours.

## Task 2. Fuzz the other parsers of applications

For each application under `apps/` that parses a response itself (look for
`app_on_data`; skip those that only use `pick` and `app_on_picked`):

1. Write `fuzz/fuzz_<app>.c` that drives the application's parsing function with the
   input in random pieces, against the stand-in host `sdk/check/standin.c` of the firmware.
2. Seed with the bodies in `tests/<app>*/` mocks.
3. 1 hour each.

## Task 3. Network faults, every application

For each application, make scenarios under `tests-agent/<app>-faults/` copied from its
existing scenario, with mocks that answer: HTTP 500, 404, 401, 304, an empty body, a body
cut in the middle, invalid JSON, a body ten times larger than normal, numbers out of
range (negative, huge, with exponent), strings with 300 characters and with emoji.

1. Run `$PERINKPHERAL/tools/perinkpheral sim-test tests-agent --update`.
2. Look at every picture. Expected: one of the standard reasons (Loading, Not set up,
   No network, Access denied, No answer) or the last good data; no crash, no text over
   other text, no text cut inside a letter, no garbage characters.
3. List every picture that looks wrong, with its path and what is wrong.

## Task 4. Long values in settings

For each application with settings in `manifest.json`, make a scenario with every text
setting at its maximum length (255 bytes, UTF-8 Cyrillic included), every list setting
full, and choices at each option. Look at the pictures as in Task 3.

## Task 5. Time edges

For applications that show time or dates (clock, calendar, sun, timer, stopwatch,
pomodoro, weather), make scenarios at: a DST change in Europe/Berlin and
America/New_York, New Year's Eve 23:59, 29 February, the clock jumping back one hour.
Use the scenario's `at` and `tz`. Look at the pictures; report wrong dates, wrong
weekday names, negative durations.

## When all tasks are done

Write a short summary at the top of `REPORT.md`: findings per task, and the three that
matter most. Then stop.
