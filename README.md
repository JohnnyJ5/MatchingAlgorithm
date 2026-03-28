# MatchingAlgo

A C++ implementation of three classical matching algorithms applied to **dating and relationship matching**: **Stable Matching** (Gale-Shapley), **Compatibility-Based Matching** (Hopcroft-Karp + Hungarian Algorithm), and **Non-Binary General Matching** (Edmonds’ Blossom Algorithm).

-----

## Project Structure

```
MatchingAlgo/
├── README.md
├── CMakeLists.txt
├── stable_matching/
│   ├── gale_shapley.h
│   ├── gale_shapley.cpp
│   └── tests/
│       └── test_gale_shapley.cpp
├── bipartite_matching/
│   ├── hopcroft_karp.h
│   ├── hopcroft_karp.cpp
│   ├── hungarian.h
│   ├── hungarian.cpp
│   └── tests/
│       └── test_bipartite.cpp
├── blossom_matching/
│   ├── blossom.h
│   ├── blossom.cpp
│   └── tests/
│       └── test_blossom.cpp
└── examples/
    ├── dating_app.cpp
    ├── group_dating.cpp
    └── inclusive_dating.cpp
```

-----

## Module 1 — Stable Matching (Gale-Shapley)

### Problem

Given a pool of singles, each with a ranked list of preferences for potential partners, find a **stable matching** — one where no two people would both prefer each other over their current match (no “would-be couples” are left unmatched against their will).

### Real-World Analogy

Imagine 100 people on a dating app. Each person ranks the others they’re interested in. A stable match means there’s no pair of people who are both unmatched with each other yet secretly prefer each other — no one has a reason to abandon their match.

### Algorithm

The Gale-Shapley algorithm works as follows:

1. Every unmatched person in the **proposing group** sends a date request to their top remaining choice.
1. Each person in the **receiving group** tentatively accepts the best request they’ve received, rejecting the rest.
1. Rejected proposers move on to their next choice.
1. Repeat until everyone is matched.

The algorithm always terminates in **O(n²)** rounds and always produces a stable matching. The result is **proposer-optimal** — every proposer ends up with the best partner they could get in any stable outcome.

### Complexity

|Metric|Value                     |
|------|--------------------------|
|Time  |O(n²)                     |
|Space |O(n²) for preference lists|

### Key Properties

- **Stability guaranteed**: No two people exist who would both rather be with each other than their current match.
- **Proposer-optimal**: The proposing side always gets their best possible stable partner.
- **Receiver-pessimal**: The receiving side always gets their worst stable partner — a known fairness tradeoff.
- **Deterministic**: Same preferences always produce the same result.

### Limitations for Dating

- Assumes complete, honest preference rankings — real users may not rank everyone.
- One side always has an advantage (proposer vs receiver asymmetry).
- Does not account for match quality scores, just ordering.

### Files

|File                         |Purpose                                        |
|-----------------------------|-----------------------------------------------|
|`gale_shapley.h`             |Class definition and interface                 |
|`gale_shapley.cpp`           |Core algorithm implementation                  |
|`tests/test_gale_shapley.cpp`|Unit tests (stability checks, edge cases, ties)|

### Planned API (C++)

```cpp
// Each person has a preference list (ranked indices of preferred partners)
// personAPrefs[i] = ordered preference list for person i in group A
// personBPrefs[j] = ordered preference list for person j in group B
GaleShapley matcher(personAPrefs, personBPrefs);
matcher.run();

// match[i] = index of person i's partner
std::vector<int> match = matcher.getMatching();

// Confirm no unstable pairs exist
bool stable = matcher.isStable();
```

-----

## Module 2 — Compatibility-Based Matching

### Problem

Rather than relying purely on ranked preferences, this module treats dating as a **scored compatibility problem**: each pair of people has a compatibility score (based on shared interests, values, location, etc.), and the goal is to find the set of pairings that maximises overall compatibility across the whole pool.

### Algorithm A — Hopcroft-Karp (Maximum Reach)

Finds the largest possible number of matched couples from a pool where not everyone is compatible with everyone else (e.g. filtered by age range, distance, or orientation).

1. Build a bipartite graph where an edge exists between two people only if they meet each other’s basic criteria.
1. Use BFS to find shortest augmenting paths (unmatched → matched chains).
1. Use DFS to extend as many paths simultaneously as possible.
1. Repeat until no more people can be matched.

**Use this when:** You want to maximise the number of people who get any match at all, regardless of quality.

### Algorithm B — Hungarian Algorithm (Optimal Compatibility)

Assigns partners to maximise the total compatibility score across all pairs — the globally best set of matches.

1. Build an n×n compatibility score matrix between all people.
1. Subtract row and column maxima to find relative scores.
1. Find a zero-cost assignment covering all people.
1. If none exists, adjust the matrix and repeat until optimal assignment is found.

**Use this when:** Match quality matters more than match quantity — you’d rather have fewer, better-matched couples than many mediocre ones.

### Complexity

|Algorithm    |Time  |Space   |Best For                          |
|-------------|------|--------|----------------------------------|
|Hopcroft-Karp|O(E√V)|O(V + E)|Maximise number of couples        |
|Hungarian    |O(n³) |O(n²)   |Maximise total compatibility score|

### Files

|File                      |Purpose                                       |
|--------------------------|----------------------------------------------|
|`hopcroft_karp.h`         |Interface for maximum reach matching          |
|`hopcroft_karp.cpp`       |BFS + DFS augmenting path implementation      |
|`hungarian.h`             |Interface for optimal compatibility assignment|
|`hungarian.cpp`           |Cost matrix reduction implementation          |
|`tests/test_bipartite.cpp`|Unit tests for both algorithms                |

### Planned API (C++)

```cpp
// Hopcroft-Karp: match as many people as possible (filtered by basic criteria)
HopcroftKarp hk(groupASize, groupBSize);
hk.addCompatiblePair(personA, personB); // add edge if they meet each other's filters
int totalCouples = hk.maxMatching();
std::vector<int> matches = hk.getMatching(); // matches[a] = b

// Hungarian: maximise total compatibility score
// compatibilityMatrix[i][j] = score between person i and person j
Hungarian hung(compatibilityMatrix);
hung.solve();
int totalScore = hung.getMaxScore();
std::vector<int> optimalMatches = hung.getAssignment(); // assignment[i] = j
```

-----

## Module 3 — Edmonds’ Blossom Algorithm (General Graph Matching)

### Problem

The previous two modules assume a **two-sided pool** (Group A matches with Group B). But in many real dating contexts — same-sex matching, non-binary users, or any app where anyone can match with anyone — there are no sides. Everyone is a node in a single graph, and any two people can potentially be paired.

Edmonds’ Blossom Algorithm solves **maximum matching on a general (non-bipartite) graph**, making it the right tool for inclusive, unrestricted dating pools.

### Real-World Analogy

Imagine an app where every user can match with any other user regardless of gender or orientation. Everyone has a list of people they’re compatible with. The goal is to pair up as many people as possible. This is exactly the general graph maximum matching problem.

### Why Bipartite Algorithms Fail Here

Hopcroft-Karp and Hungarian require two distinct sides. If you try to force a general graph into bipartite form, you miss valid matches. The critical obstacle is **odd-length cycles** (called “blossoms”) — bipartite graphs have none, but general graphs do, and naïve augmenting path search gets stuck in them.

Edmonds’ insight was to **contract blossoms** into single nodes, find augmenting paths in the contracted graph, then expand the contraction back to recover the full matching.

### Algorithm

1. Start with an empty matching.
1. For each unmatched person, run a BFS/DFS to find an **augmenting path** (a path that starts and ends at unmatched people, alternating between unmatched and matched edges).
1. If an **odd cycle (blossom)** is detected during the search, contract it into a single node and continue searching in the reduced graph.
1. Once an augmenting path is found, **flip** all edges along it (unmatched → matched, matched → unmatched), increasing the matching size by 1.
1. Expand all contracted blossoms to recover the true matching.
1. Repeat until no augmenting path exists.

### Complexity

|Metric|Value                                                |
|------|-----------------------------------------------------|
|Time  |O(V³) naïve; O(E√V) with Micali-Vazirani optimisation|
|Space |O(V + E)                                             |

### Key Properties

- **Works on any graph**: No assumption of two sides — anyone can match with anyone.
- **Maximum cardinality**: Produces the largest possible number of couples.
- **Handles odd cycles**: The blossom contraction step is what makes this possible; no other general matching algorithm avoids this.
- **Extensible to weighted matching**: With the Galil-Micali-Gabow variant, also finds minimum-weight or maximum-weight perfect matchings.

### Files

|File                    |Purpose                                                              |
|------------------------|---------------------------------------------------------------------|
|`blossom.h`             |Class definition and interface                                       |
|`blossom.cpp`           |Augmenting path search + blossom contraction/expansion               |
|`tests/test_blossom.cpp`|Unit tests (odd cycles, disconnected graphs, max matching validation)|

### Planned API (C++)

```cpp
// Single pool — no sides, anyone can match anyone
Blossom matcher(numPeople);
matcher.addCompatiblePair(personA, personB); // undirected edge

int totalCouples = matcher.maxMatching();
std::vector<int> match = matcher.getMatching();
// match[i] = j means person i is matched with person j
// match[i] = -1 means person i is unmatched
```

-----

## Build Instructions

```bash
mkdir build && cd build
cmake ..
make

# Run tests
./stable_matching/tests/test_gale_shapley
./bipartite_matching/tests/test_bipartite
./blossom_matching/tests/test_blossom

# Run examples
./examples/dating_app
./examples/group_dating
./examples/inclusive_dating
```

**Requirements:** C++17 or later, CMake 3.15+

-----

## Examples

### `examples/dating_app.cpp`

Simulates a dating app with a pool of users:

- Gale-Shapley: users rank their top matches by preference; algorithm finds a stable pairing.
- Hopcroft-Karp: users set filters (age, distance, interests); algorithm matches as many people as possible within those constraints.
- Hungarian: a compatibility score is computed per pair; algorithm finds the globally optimal set of couples.

### `examples/group_dating.cpp`

Simulates a speed-dating or group event scenario:

- Multiple rounds of proposals.
- Tracks which pairings are stable across the group.
- Demonstrates how adding a new person to the pool can disrupt existing stable matches.

### `examples/inclusive_dating.cpp`

Simulates an open, non-bipartite dating pool:

- All users are nodes in a single graph with no group separation.
- Compatibility edges are added between any two mutually interested users.
- Blossom algorithm finds the maximum number of couples across the entire pool.
- Demonstrates blossom contraction on an example with an odd cycle.

-----

## Algorithm Comparison

|Property            |Gale-Shapley           |Hopcroft-Karp             |Hungarian                 |Blossom                     |
|--------------------|-----------------------|--------------------------|--------------------------|----------------------------|
|Input               |Ranked preference lists|Compatibility filter graph|Compatibility score matrix|General compatibility graph |
|Output              |Stable couples         |Max number of couples     |Globally optimal couples  |Max couples (any graph)     |
|Optimality          |Proposer-optimal       |Maximum cardinality       |Highest total score       |Maximum cardinality         |
|Time complexity     |O(n²)                  |O(E√V)                    |O(n³)                     |O(V³)                       |
|Handles scores      |No                     |No                        |Yes                       |No (weighted variant exists)|
|Requires two sides  |Yes                    |Yes                       |Yes                       |**No**                      |
|Guarantees stability|Yes                    |No                        |No                        |No                          |
|Best used when      |Users rank each other  |Hard filters + two sides  |Score-based + two sides   |Anyone can match anyone     |

-----

## Design Notes

- **Group A / Group B** in Modules 1 and 2 can represent any two-sided pool. The algorithms are agnostic to how the groups are defined.
- **Module 3 (Blossom)** removes the two-sided constraint entirely, making it the right choice for inclusive apps where any user can match with any other user.
- Compatibility scores in the Hungarian module can be computed from any feature vector (age gap, shared interests, location proximity, etc.) — the algorithm itself is score-agnostic.
- A natural next step is **weighted Blossom** (Galil-Micali-Gabow), which finds maximum-weight general matchings — combining the inclusivity of Blossom with the score optimisation of Hungarian.

-----

## References

- Gale, D. & Shapley, L. S. (1962). *College Admissions and the Stability of Marriage.* The American Mathematical Monthly.
- Hopcroft, J. E. & Karp, R. M. (1973). *An n^(5/2) Algorithm for Maximum Matchings in Bipartite Graphs.* SIAM Journal on Computing.
- Kuhn, H. W. (1955). *The Hungarian Method for the Assignment Problem.* Naval Research Logistics.
- Edmonds, J. (1965). *Paths, Trees, and Flowers.* Canadian Journal of Mathematics.