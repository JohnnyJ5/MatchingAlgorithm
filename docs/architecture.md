# Architecture Notes

## Overview

Spark has two independent entry points that share one algorithm library:

| Entry point | File | Use case |
|-------------|------|----------|
| **CLI tool** | `main.cpp` | Local comparative analysis; no network, no DB |
| **REST API** | `server/server.cpp` | Full platform with PostgreSQL + SPA UI |

Both compile against `libalgoritms.a` (built from `src/`).

---

## Component Diagram

```
┌───────────────────────────────────────────────────────────────────────┐
│  Docker Compose Stack                                                 │
│                                                                       │
│  ┌─────────────────────────────────────────────┐                     │
│  │  app container  (port 8081)                 │                     │
│  │                                             │                     │
│  │  ┌──────────────────────────────────────┐  │                     │
│  │  │  Crow HTTP Server  (server.cpp)      │  │                     │
│  │  │  ┌────────────┐  ┌───────────────┐  │  │                     │
│  │  │  │  Route     │  │  matchesToJson│  │  │                     │
│  │  │  │  handlers  │  │  helper       │  │  │                     │
│  │  │  └─────┬──────┘  └───────────────┘  │  │                     │
│  │  │        │                             │  │                     │
│  │  │  ┌─────▼──────────────────────────┐ │  │                     │
│  │  │  │  libalgoritms.a                │ │  │                     │
│  │  │  │  GaleShapley · HopcroftKarp    │ │  │                     │
│  │  │  │  Hungarian   · Blossom         │ │  │                     │
│  │  │  └────────────────────────────────┘ │  │                     │
│  │  │                                     │  │                     │
│  │  │  ┌────────────────────────────────┐ │  │                     │
│  │  │  │  DBManager + ConnPool          │ │  │                     │
│  │  │  │  (db_manager.cpp, conn_pool.cpp)│ │  │                     │
│  │  │  └─────────────┬──────────────────┘ │  │                     │
│  │  └────────────────┼────────────────────┘  │                     │
│  │                   │  libpq                │                     │
│  └───────────────────┼───────────────────────┘                     │
│                      │                                               │
│  ┌───────────────────▼───────────────────────┐                     │
│  │  db container  (port 5432)                │                     │
│  │  PostgreSQL 16                            │                     │
│  │  schema: users · questions ·              │                     │
│  │  questionnaire_submissions ·              │                     │
│  │  compatibility_scores · events ·          │                     │
│  │  event_participants · algorithm_runs ·    │                     │
│  │  matches · messages                       │                     │
│  └───────────────────────────────────────────┘                     │
└───────────────────────────────────────────────────────────────────────┘

  Browser ──── HTTP/JSON ──── app:8081
```

---

## Threading Model

Crow spawns one thread per request. PostgreSQL connections are **not thread-safe** — sharing a single connection with a mutex is explicitly prohibited (see `CLAUDE.local.md`).

```
Request 1 ──► Crow thread 1 ──► ConnPool.acquire() ──► conn A ──► PG
Request 2 ──► Crow thread 2 ──► ConnPool.acquire() ──► conn B ──► PG
Request 3 ──► Crow thread 3 ──► ConnPool.acquire() ──► conn C ──► PG  (waits if pool exhausted)
```

`conn_pool.h/cpp` maintains a fixed-size pool of `libpq` connections. Each handler acquires a connection, executes queries inside a transaction, and releases it back on scope exit (RAII).

For contention on shared rows use `SELECT ... FOR UPDATE` or PostgreSQL advisory locks — never application-level mutexes.

---

## Data Flow: Questionnaire → Algorithm → Match

```
User submits answers
        │
        ▼
POST /api/questionnaire/:id
        │
        ▼
DBManager::upsertAnswers()
  INSERT INTO questionnaire_submissions (user_id, answers)
        │
        ▼  (PostgreSQL trigger fires)
  recompute_compatibility_scores()
  UPDATE compatibility_scores SET score = dot_product(answers_a, answers_b)
        │
        ▼
Organiser runs algorithm
POST /api/algorithms/hungarian
        │
        ▼
DBManager::fetchCompatibilityMatrix()
  SELECT user_a, user_b, score FROM compatibility_scores ORDER BY user_a, user_b
        │
        ▼
Hungarian(matrix).solve()   ← pure in-memory, no DB
        │
        ▼
DBManager::storeAlgorithmRun()
  INSERT INTO algorithm_runs (event_id, algorithm, result)
        │
        ▼
200 {pairs, totalScore, avgScore}
        │
        ▼
Both users accept → trigger sets matches.status = 'accepted'
        │
        ▼
Messaging unlocked
```

---

## Algorithm Library Interface

All four algorithms share the same output shape, making downstream code (JSON serialisation, comparison runner) algorithm-agnostic:

```cpp
// Universal output: result[i] = j  (matched)  or  -1  (unmatched)
std::vector<int> result;

// Gale-Shapley
GaleShapley gs(prefsA, prefsB);
gs.run();
result = gs.getMatching();      // result[man] = woman

// Hopcroft-Karp
HopcroftKarp hk(sizeA, sizeB);
for (auto [a, b] : compatiblePairs) hk.addEdge(a, b);
hk.maxMatching();
result = hk.getMatching();      // result[a] = b

// Hungarian
Hungarian hung(scoreMatrix);
hung.solve();
result = hung.getAssignment();  // result[i] = j

// Blossom (bipartite encoding: men=0..N-1, women=N..2N-1)
Blossom bl(2 * N);
for (int m = 0; m < N; ++m)
    for (int w = 0; w < N; ++w)
        if (COMPAT[m][w] >= THRESHOLD) bl.addEdge(m, w + N);
bl.maxMatching();
auto raw = bl.getMatching();
for (int m = 0; m < N; ++m)
    result[m] = (raw[m] == -1) ? -1 : raw[m] - N;
```

### Internal Optimisations

| Algorithm | Key optimisation |
|-----------|-----------------|
| GaleShapley | Pre-computed rank tables `rankA_[i][j]` — O(1) preference lookup during proposals instead of O(n) scan |
| Hungarian | Negates score matrix internally (minimise cost = maximise score); `maxScore_` holds the true optimum |
| Blossom | Blossom contraction handles odd cycles that would stall naïve augmenting-path search |
| HopcroftKarp | BFS finds all shortest augmenting paths in one pass; DFS extends them simultaneously |

---

## Build Pipeline

```
docker compose run --rm dev   (CMake configure + ninja/make)
        │
        ├── main            links: algorithms
        ├── server          links: algorithms + Crow + libpq
        ├── test_gale_shapley
        ├── test_hopcroft_karp
        ├── test_hungarian
        ├── test_blossom
        ├── test_db_types
        ├── test_conn_pool
        └── test_db_manager[_integration]
```

External dependencies (no vendoring):

| Dependency | Version | Fetched via |
|------------|---------|-------------|
| Crow | 1.2.1 | CMake `FetchContent` |
| GoogleTest | 1.14.0 | CMake `FetchContent` |
| libpq | system | Debian package in Dockerfile |

---

## Docker Image Stages

```
┌─────────────────────────────────────────────┐
│  Stage 1: dev                               │
│  debian:bookworm-slim + g++ cmake libpq-dev │
│  Used by: docker-compose dev service        │
│  Purpose: interactive development shell     │
└─────────────────────────────────────────────┘
         │ (base for)
┌────────▼────────────────────────────────────┐
│  Stage 2: builder                           │
│  Copies source → cmake configure → compile  │
│  Used by: CI                                │
└─────────────────────────────────────────────┘
         │ (copies binary from)
┌────────▼────────────────────────────────────┐
│  Stage 3: runtime                           │
│  debian:bookworm-slim + libstdc++6 + libpq5 │
│  ENTRYPOINT: /app/server                    │
│  Used by: docker-compose app service        │
│  Size: ~80 MB (no toolchain)                │
└─────────────────────────────────────────────┘
```

---

## Security Model

- All SQL queries are parameterised — no string concatenation.
- Row Level Security (RLS) is enabled on sensitive tables (`users`, `messages`, `matches`).
- The application sets `app.current_user_id` and `app.current_role` session variables; RLS policies inspect these.
- Three DB roles: `app_user` (regular users), `app_admin` (organisers), `app_scoring_service` (trigger/score recomputation).
- PII columns (`real_name`, `email`, `password_hash`) are marked `RESTRICTED` and excluded from general `SELECT *` queries via column-level privileges.
- Identities are hidden from matched users until both accept (`status='accepted'`).
