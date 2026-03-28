# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
# Build everything (main driver + all test binaries)
make

# Build only the main driver
make build/main

# Run the main driver (blind dating comparative analysis)
make run

# Build and run all unit tests
make run_tests

# Run a single test binary
./build/test_gale_shapley
./build/test_hopcroft_karp
./build/test_hungarian
./build/test_blossom

# Clean
make clean
```

**Requirements:** C++17, `g++`

## Architecture

All algorithm implementations live in `src/`. There is no CMake integration in active use — the `Makefile` is the build system.

### Source Layout

```
src/
  gale_shapley.{h,cpp}    — Module 1: Stable Matching (Gale-Shapley)
  hopcroft_karp.{h,cpp}   — Module 2a: Maximum Bipartite Matching (Hopcroft-Karp)
  hungarian.{h,cpp}        — Module 2b: Optimal Assignment (Hungarian Algorithm)
  blossom.{h,cpp}          — Module 3: General Graph Matching (Edmonds' Blossom)
  tests/
    test_gale_shapley.cpp
    test_hopcroft_karp.cpp
    test_hungarian.cpp
    test_blossom.cpp
main.cpp                   — Comparative analysis driver (blind dating scenario)
```

### Module Summary

| Class | Input | Output | Guarantee |
|---|---|---|---|
| `GaleShapley` | Two ranked preference lists (same size n) | `matchA[i] = j` | Stable, proposer-optimal |
| `HopcroftKarp` | Bipartite graph (two groups, compatible pairs as edges) | `match[a] = b`, -1 if unmatched | Maximum cardinality |
| `Hungarian` | n×n compatibility score matrix | `assignment[i] = j` | Globally optimal total score |
| `Blossom` | General graph (single vertex set, undirected edges) | `match[i] = j`, -1 if unmatched | Maximum cardinality, handles odd cycles |

### Key Design Decisions

- **`GaleShapley`** stores group B preferences as a rank lookup table (`rankB_[j][i]`) rather than a preference list, giving O(1) comparison during proposals.
- **`Blossom`** uses union-find for blossom contraction. Men/women are encoded as vertices 0..N-1 and N..2N-1 respectively when used on bipartite data; the caller translates indices back (see `main.cpp:runBlossom()`).
- All four algorithms are exercised in `main.cpp` on the same 6×6 compatibility dataset and their results compared side-by-side.
- The `build/` directory (gitignored) is flat — all object files and test binaries land directly in `build/`, not in subdirectories.
