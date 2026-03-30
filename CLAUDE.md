# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

All commands run inside the Docker dev container. The `dev` service bind-mounts the project root at `/app`; build artifacts go into a named Docker volume (`dev_build`) that is **not** directly accessible from the host `./build/` directory.

**Setup:** copy `.env.example` to `.env` (or set `DB_NAME`, `DB_USER`, `DB_PASSWORD` in environment) before starting the stack.

```bash
# Build everything (main driver + server + all test binaries)
make all

# Build and run the CLI comparative analysis
make run

# Start the full stack (app + postgres), server at http://localhost:8081
make run_server       # foreground via docker compose up app
make docker-start     # detached
make docker-stop

# Build and run all unit tests
make run_tests

# Run a single test binary
docker compose run --rm dev ./build/test_gale_shapley

# Clean build artifacts
make clean
```

**Requirements:** Docker with Compose plugin.

The Dockerfile has three stages: `dev` (toolchain only, used by the `dev` service), `builder` (copies source and compiles, used for CI), and `runtime` (server binary + static files only, used by the `app` service).

## Architecture

The project has two entry points sharing one algorithm library:

- **`main.cpp`** — CLI tool that runs all four algorithms on a hardcoded 6×6 dataset and prints a side-by-side comparison.
- **`server/server.cpp`** — Crow-based REST API + single-page web UI ("Spark") that exposes the same algorithms over HTTP on port 9090.

Both still contain a hardcoded 6×6 `COMPAT[man][woman]` score matrix and the same preference-building logic (sort columns descending to derive ordinal rankings from raw scores). **The PostgreSQL database has been added** (`db/schema.sql`, `compatibility_scores` table) — these hardcoded matrices are the primary place to replace with DB queries.

### Source Layout

```
src/                              — Algorithm static library (libalgoritms.a)
  gale_shapley.{h,cpp}            — Stable Matching
  hopcroft_karp.{h,cpp}           — Maximum Bipartite Matching
  hungarian.{h,cpp}               — Optimal Assignment
  blossom.{h,cpp}                 — General Graph Matching
  tests/                          — One test binary per algorithm
main.cpp                          — CLI comparative analysis driver
server/
  server.cpp                      — REST API (Crow framework, ~656 lines)
  static/index.html               — Single-page app (embedded CSS + JS)
  BRANDING.md                     — UI design system reference
db/
  schema.sql                      — PostgreSQL 16 schema (users, matches, events, messages, scores)
```

### Module Summary

| Class | Input | Output | Guarantee |
|---|---|---|---|
| `GaleShapley` | Two ranked preference lists (size n) | `matchA[i] = j` | Stable, proposer-optimal |
| `HopcroftKarp` | Bipartite graph (edges where score ≥ threshold) | `match[a] = b`, -1 if unmatched | Maximum cardinality |
| `Hungarian` | n×n score matrix | `assignment[i] = j` | Globally optimal total score |
| `Blossom` | General graph (undirected edges) | `match[i] = j`, -1 if unmatched | Maximum cardinality, handles odd cycles |

All four algorithms output the same shape: `vector<int>` where `result[i] = j` (matched) or `-1` (unmatched). This uniform interface is what makes `main.cpp`'s side-by-side comparison and `server.cpp`'s `matchesToJson()` helper straightforward.

### Key Design Decisions

- **`GaleShapley`** pre-computes rank lookup tables (`rankA_[i][j]`, `rankB_[j][i]`) so preference comparisons during proposals are O(1) rather than O(n).
- **`Blossom` on bipartite data**: The caller encodes men as vertices `0..N-1` and women as `N..2N-1`, then translates back after the run: `blossom.getMatching()[m] - N` gives the woman index. See `runBlossom()` in both `main.cpp` and `server.cpp`.
- **`Hungarian`** negates scores internally (minimizes cost = maximizes score). The `maxScore_` field is the true maximum.
- Algorithms are stateless at construction and safe to re-run; each `.run()` / `.solve()` / `.maxMatching()` call re-initializes internal state.
- Crow is fetched at configure time via CMake `FetchContent` (v1.2.1). No vendored dependencies in the repo.

### Build System

CMake is the build system; the `Makefile` is a thin wrapper that delegates to `docker compose run --rm dev`. Root `CMakeLists.txt` fetches Crow and adds two subdirectories:

- `src/CMakeLists.txt` → static lib `algorithms` + 4 test executables
- `server/CMakeLists.txt` → `server` executable (links `algorithms` + Crow)

`main.cpp` is built directly from the root `CMakeLists.txt` and links `algorithms`.

### Database

PostgreSQL 16 (`db` service in docker-compose). The schema is applied automatically from `db/schema.sql` on first container start. Key tables:

- **`users`** — identity + profile; soft-deleted via `deleted_at`; PII columns (`real_name`, `email`, `password_hash`) are RESTRICTED
- **`compatibility_scores`** — materialized N×N score matrix; the C++ algorithms read this instead of the hardcoded matrix
- **`questionnaire_submissions`** — JSONB answer vectors; triggers score recomputation
- **`algorithm_runs`** — caches JSONB algorithm output per event
- **`matches`** — pending/accepted/declined pairings; `sync_match_status` trigger auto-sets `status='accepted'` when both flags are TRUE
- **`messages`** — chat history between accepted matches; cursor-based pagination via PK

Row Level Security is enabled on sensitive tables. The app sets `app.current_user_id` / `app.current_role` session variables to drive RLS policies.
