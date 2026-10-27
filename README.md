# Chrono VCS

A version control system for spreadsheets, written from scratch in C++17 for our Object-Oriented Programming course project.

Chrono saves snapshots of your files, lets you browse and restore earlier versions, and understands spreadsheet formulas. The goal is to catch the merge mistakes Git can't see: two people edit different cells, the merge looks clean, but one change silently alters the other's results.

## Status

| Phase | What | Status |
|---|---|---|
| 0 | Project setup, shared interfaces | Done |
| 1 | Commits, history, storage, checkout and revert; cells, grid and formula engine | Done |
| 2 | Branches, hashing, integrity check, line diff; dependency graph, CSV | In progress |
| 3 | Three-way merge, undo; cell-level diff and merge | Planned |
| 4 | Plain-English commands; semantic conflict detection | Planned |

Work is tracked in [GitHub issues](https://github.com/swarajdeshmukh4249/Chronos-VCS/issues) and [milestones](https://github.com/swarajdeshmukh4249/Chronos-VCS/milestones).

## Building

You need CMake 3.16 or newer and a C++17 compiler (GCC, Clang, MSVC or MinGW).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds three programs in `build/`:

| Program | What it is |
|---|---|
| `chrono` | The command-line tool |
| `core_tests` | Tests for the version control core |
| `sheet_tests` | Tests for the spreadsheet engine |

Every pull request is built and tested automatically on Linux, macOS and Windows.

## Using it

```sh
chrono init                    # start tracking history in this folder
chrono add notes.txt           # include a file in future commits
chrono commit -m "first draft" # save a new version of every tracked file
chrono log                     # list commits, newest first (* marks the current one)
chrono status                  # check for uncommitted changes
chrono checkout 1              # put the files from commit 1 back
chrono revert 1                # put commit 1's files back and record it as a new commit
```

The author name comes from the `CHRONO_AUTHOR` environment variable, then `USER` or `USERNAME`.

Example session:

```
$ chrono init
Started a Chrono repository in /home/swaraj/budget
$ chrono add notes.txt
Tracking notes.txt
$ chrono commit -m "first draft"
Saved commit #1 (1 file)
$ chrono commit -m "updated totals"      # after editing notes.txt
Saved commit #2 (1 file)
$ chrono revert 1
Saved commit #3: Revert to #1: first draft
$ chrono log
* #3  2026-10-27 10:45  Swaraj  Revert to #1: first draft
  #2  2026-10-27 10:45  Swaraj  updated totals
  #1  2026-10-27 10:45  Swaraj  first draft
```

History is stored as plain text under `.chrono/`, so you can open it and see exactly what Chrono saved.

## Project layout

```
include/common/   interfaces and exceptions shared by both layers
src/core/         version control: commits, history, storage, repository
src/sheet/        spreadsheet engine: cells, grid, formulas, evaluation
src/nlp/          plain-English commands (Phase 4)
src/main.cpp      command-line entry point
tests/            test suites for each layer, sharing tests/common/
```

## OOP concepts used

| Concept | Where |
|---|---|
| Encapsulation | `Commit`, `Cell`, `Repository` keep their data private |
| Constructor overloading | `Cell()`, `Cell(double)`, `Cell(int)`, `Cell(std::string)` |
| Abstract classes and runtime polymorphism | `Formula` (evaluated through `formula->evaluate()`), `MergeStrategy`, `DiffAlgorithm` |
| Templates | `History<T>`, `Grid<T>` |
| Exceptions | One `ChronoException` hierarchy in `include/common/Exceptions.h` |
| File handling | `StorageManager` and `WorkingTree` |
| Design patterns | Interpreter (`Formula` tree), Strategy (merge and diff) |
| Single Responsibility | `Repository` holds the rules; `StorageManager` owns `.chrono/`; `WorkingTree` owns your files |

## Team

| | Area |
|---|---|
| Swaraj ([swarajdeshmukh4249](https://github.com/swarajdeshmukh4249)) | Version control core and plain-English commands |
| Tanishka ([tanishkatanawade](https://github.com/tanishkatanawade)) | Spreadsheet engine and semantic merge |
