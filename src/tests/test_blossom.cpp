// GTest suite for Edmonds' Blossom algorithm (general graph maximum matching).
//
// Scenario: the compatibility graph is treated as a general undirected graph.
// Edges are added only between men and women (bipartite structure), but the
// algorithm enforces no such constraint — odd-cycle tests use non-bipartite
// graphs to exercise the blossom contraction path.
//
// Vertex encoding (bipartite tests): men 0..n-1, women n..2n-1.

#include "../blossom.h"

#include <gtest/gtest.h>
#include <vector>

// Helper: asserts the matching vector is self-consistent (symmetric, no
// self-loops).
static void expectValidMatching(const std::vector<int>& m) {
    const int n = static_cast<int>(m.size());
    for (int i = 0; i < n; ++i) {
        if (m[i] == -1) continue;
        EXPECT_NE(m[i], i) << "vertex " << i << " matched to itself";
        EXPECT_EQ(m[m[i]], i) << "matching not symmetric at " << i;
    }
}

// ── 2-couple perfect ──────────────────────────────────────────────────────────

TEST(BlossomTest, TwoCouplesPerfect) {
    Blossom b(4);
    b.addCompatiblePair(0, 2);  // M0↔W0
    b.addCompatiblePair(1, 3);  // M1↔W1

    EXPECT_EQ(b.maxMatching(), 2);
    expectValidMatching(b.getMatching());
}

// ── No compatible pairs ───────────────────────────────────────────────────────

TEST(BlossomTest, NoCompatiblePairs) {
    Blossom b(6);
    EXPECT_EQ(b.maxMatching(), 0);
}

// ── Fully compatible pool ─────────────────────────────────────────────────────

TEST(BlossomTest, FullyCompatiblePool) {
    Blossom b(6);
    for (int m = 0; m < 3; ++m)
        for (int w = 3; w < 6; ++w)
            b.addCompatiblePair(m, w);

    EXPECT_EQ(b.maxMatching(), 3);
    expectValidMatching(b.getMatching());
}

// ── Shared top choice (bottleneck) ───────────────────────────────────────────

// M0 compat W0 only.  M1 compat W0 and W1.  M2 compat W1 only.  Max = 2.
TEST(BlossomTest, SharedTopChoice) {
    Blossom b(5);
    b.addCompatiblePair(0, 3);
    b.addCompatiblePair(1, 3);
    b.addCompatiblePair(1, 4);
    b.addCompatiblePair(2, 4);

    EXPECT_EQ(b.maxMatching(), 2);
    expectValidMatching(b.getMatching());
}

// ── More women than men ───────────────────────────────────────────────────────

TEST(BlossomTest, MoreWomenThanMen) {
    Blossom b(8);
    b.addCompatiblePair(0, 3);
    b.addCompatiblePair(1, 5);
    b.addCompatiblePair(2, 7);

    EXPECT_EQ(b.maxMatching(), 3);
    expectValidMatching(b.getMatching());
}

// ── Augmenting path required ──────────────────────────────────────────────────

// Chain of compatible pairs — greedy may stall; augmenting paths find 4.
TEST(BlossomTest, AugmentingPathRequired) {
    Blossom b(8);
    b.addCompatiblePair(0, 4);
    b.addCompatiblePair(1, 4);
    b.addCompatiblePair(1, 5);
    b.addCompatiblePair(2, 5);
    b.addCompatiblePair(2, 6);
    b.addCompatiblePair(3, 6);
    b.addCompatiblePair(3, 7);

    EXPECT_EQ(b.maxMatching(), 4);
    expectValidMatching(b.getMatching());
}

// ── Single compatible pair ────────────────────────────────────────────────────

TEST(BlossomTest, SingleCompatiblePair) {
    Blossom b(2);
    b.addCompatiblePair(0, 1);
    EXPECT_EQ(b.maxMatching(), 1);
}

// ── n=1 single vertex ─────────────────────────────────────────────────────────

TEST(BlossomTest, N1SingleVertexNoMatch) {
    Blossom b(1);
    EXPECT_EQ(b.maxMatching(), 0);
    EXPECT_EQ(b.getMatching()[0], -1);
}

// ── Two disjoint pairs ────────────────────────────────────────────────────────

// 4 vertices: edges 0-1 and 2-3 are in separate components — both must match.
TEST(BlossomTest, TwoDisjointPairs) {
    Blossom b(4);
    b.addCompatiblePair(0, 1);
    b.addCompatiblePair(2, 3);

    EXPECT_EQ(b.maxMatching(), 2);
    expectValidMatching(b.getMatching());
    const auto& m = b.getMatching();
    EXPECT_EQ(m[0], 1);
    EXPECT_EQ(m[1], 0);
    EXPECT_EQ(m[2], 3);
    EXPECT_EQ(m[3], 2);
}

// ── Triangle (odd 3-cycle) ────────────────────────────────────────────────────

// The canonical odd-cycle case that requires blossom contraction.
// Maximum matching = 1 (one vertex must be left out).
// An algorithm that mishandles odd cycles could return 2 or loop.
TEST(BlossomTest, TriangleOddCycle) {
    Blossom b(3);
    b.addCompatiblePair(0, 1);
    b.addCompatiblePair(1, 2);
    b.addCompatiblePair(0, 2);

    EXPECT_EQ(b.maxMatching(), 1);
    expectValidMatching(b.getMatching());
}

// ── Pentagon (odd 5-cycle) ────────────────────────────────────────────────────

// Odd cycle of length 5. Maximum matching = 2; one vertex remains unmatched.
TEST(BlossomTest, PentagonOddCycle) {
    Blossom b(5);
    b.addCompatiblePair(0, 1);
    b.addCompatiblePair(1, 2);
    b.addCompatiblePair(2, 3);
    b.addCompatiblePair(3, 4);
    b.addCompatiblePair(4, 0);

    EXPECT_EQ(b.maxMatching(), 2);
    expectValidMatching(b.getMatching());
}

// ── Odd-length path ───────────────────────────────────────────────────────────

// Path 0-1-2-3-4: 5 vertices in a line. Optimal = 2 matches; vertex 4
// (or 0) is left unmatched. No perfect matching exists on 5 vertices.
TEST(BlossomTest, PathGraphOddLength) {
    Blossom b(5);
    b.addCompatiblePair(0, 1);
    b.addCompatiblePair(1, 2);
    b.addCompatiblePair(2, 3);
    b.addCompatiblePair(3, 4);

    EXPECT_EQ(b.maxMatching(), 2);
    expectValidMatching(b.getMatching());
}
