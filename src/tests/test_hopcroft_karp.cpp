// GTest suite for Hopcroft-Karp maximum bipartite matching.
//
// Scenario: blind one-on-one dating with a compatibility threshold. Two people
// are only paired as candidates if their questionnaire scores exceed the
// threshold — otherwise they are considered incompatible and no edge is added.
// Men occupy the left side of the bipartite graph; women the right side.

#include "../hopcroft_karp.h"

#include <gtest/gtest.h>
#include <vector>

// Helper: asserts the matching is valid — no woman is matched to two men.
static void expectValidMatching(const std::vector<int>& matchLeft, int rightSize) {
    std::vector<bool> used(rightSize, false);
    for (int i = 0; i < static_cast<int>(matchLeft.size()); ++i) {
        int w = matchLeft[i];
        if (w == -1) continue;
        EXPECT_GE(w, 0) << "matchLeft[" << i << "] out of range";
        EXPECT_LT(w, rightSize) << "matchLeft[" << i << "] out of range";
        EXPECT_FALSE(used[w]) << "Woman " << w << " matched to two men";
        if (w >= 0 && w < rightSize) used[w] = true;
    }
}

// ── Perfect matching ──────────────────────────────────────────────────────────

// Every man is compatible with exactly one woman (diagonal) — perfect matching.
TEST(HopcroftKarpTest, PerfectMatching) {
    HopcroftKarp hk(4, 4);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 2);
    hk.addCompatiblePair(3, 3);

    EXPECT_EQ(hk.maxMatching(), 4);
    expectValidMatching(hk.getMatching(), 4);
}

// ── No compatible pairs ───────────────────────────────────────────────────────

TEST(HopcroftKarpTest, NoCompatiblePairs) {
    HopcroftKarp hk(4, 4);
    EXPECT_EQ(hk.maxMatching(), 0);
}

// ── Partial compatibility ─────────────────────────────────────────────────────

// M0 compat with W0 only.  M1 compat with W0 and W1.  M2 compat with W1 only.
// Maximum matching = 2 (only 2 women available).
TEST(HopcroftKarpTest, PartialCompatibility) {
    HopcroftKarp hk(3, 2);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 1);

    EXPECT_EQ(hk.maxMatching(), 2);
    expectValidMatching(hk.getMatching(), 2);
}

// ── Fully compatible pool ─────────────────────────────────────────────────────

TEST(HopcroftKarpTest, FullyCompatiblePool) {
    const int n = 5;
    HopcroftKarp hk(n, n);
    for (int m = 0; m < n; ++m)
        for (int w = 0; w < n; ++w)
            hk.addCompatiblePair(m, w);

    EXPECT_EQ(hk.maxMatching(), n);
    expectValidMatching(hk.getMatching(), n);
}

// ── More women than men ───────────────────────────────────────────────────────

// 3 men, 5 women; each man compatible with a distinct woman. All men matched.
TEST(HopcroftKarpTest, MoreWomenThanMen) {
    HopcroftKarp hk(3, 5);
    hk.addCompatiblePair(0, 1);
    hk.addCompatiblePair(1, 3);
    hk.addCompatiblePair(2, 4);

    EXPECT_EQ(hk.maxMatching(), 3);
}

// ── Augmenting path needed ────────────────────────────────────────────────────

// Greedy assignment is suboptimal; augmenting paths are required.
// M0 compat W0.  M1 compat W0 and W1.  M2 compat W1.
// Maximum = 2 (3 men but only 2 women).
TEST(HopcroftKarpTest, AugmentingPathNeeded) {
    HopcroftKarp hk(3, 2);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 1);

    EXPECT_EQ(hk.maxMatching(), 2);
}

// ── Single compatible pair ────────────────────────────────────────────────────

TEST(HopcroftKarpTest, SinglePair) {
    HopcroftKarp hk(1, 1);
    hk.addCompatiblePair(0, 0);
    EXPECT_EQ(hk.maxMatching(), 1);
    EXPECT_EQ(hk.getMatching()[0], 0);
}

// ── More men than women ───────────────────────────────────────────────────────

// 5 men all compatible with 3 women — cap at 3.
TEST(HopcroftKarpTest, MoreMenThanWomen) {
    HopcroftKarp hk(5, 3);
    for (int m = 0; m < 5; ++m)
        for (int w = 0; w < 3; ++w)
            hk.addCompatiblePair(m, w);

    EXPECT_EQ(hk.maxMatching(), 3);
    expectValidMatching(hk.getMatching(), 3);
}

// ── Star bottleneck ───────────────────────────────────────────────────────────

// All 4 men compatible only with W0 — maximum is 1.
// Any algorithm that double-assigns a woman would fail this.
TEST(HopcroftKarpTest, StarBottleneckSingleWoman) {
    HopcroftKarp hk(4, 1);
    for (int m = 0; m < 4; ++m)
        hk.addCompatiblePair(m, 0);
    EXPECT_EQ(hk.maxMatching(), 1);
}

// ── Rerun stability ───────────────────────────────────────────────────────────

// Calling maxMatching() twice must return the same result.
TEST(HopcroftKarpTest, RerunReturnsSameResult) {
    HopcroftKarp hk(3, 3);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 2);

    const int first  = hk.maxMatching();
    const int second = hk.maxMatching();
    EXPECT_EQ(first, second);
    EXPECT_EQ(second, 3);
}
