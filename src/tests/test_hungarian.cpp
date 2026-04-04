// GTest suite for the Hungarian algorithm (optimal bipartite assignment).
//
// Scenario: blind one-on-one dating. Every man must be assigned to exactly one
// woman. The compatibility score between each pair is derived from a
// questionnaire. The Hungarian algorithm finds the assignment that maximises
// the *total* combined compatibility score across all couples.

#include "../hungarian.h"

#include <gtest/gtest.h>
#include <vector>

// Helper: verifies reported score equals sum of assigned pair scores.
static void expectScoreConsistent(const std::vector<std::vector<int>>& mat,
                                   const Hungarian& h) {
    int total = 0;
    const auto& a = h.getAssignment();
    for (int i = 0; i < static_cast<int>(a.size()); ++i)
        total += mat[i][a[i]];
    EXPECT_EQ(total, h.getMaxScore());
}

// ── 2-couple clear winner ─────────────────────────────────────────────────────

TEST(HungarianTest, TwoCouplesClearWinner) {
    std::vector<std::vector<int>> compat = {
        {90, 10},
        {10, 90},
    };
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 180);
    EXPECT_EQ(h.getAssignment()[0], 0);
    EXPECT_EQ(h.getAssignment()[1], 1);
    expectScoreConsistent(compat, h);
}

// ── 3-couple diagonal ─────────────────────────────────────────────────────────

TEST(HungarianTest, ThreeCouplesDiagonal) {
    std::vector<std::vector<int>> compat = {
        {80,  5,  5},
        { 5, 80,  5},
        { 5,  5, 80},
    };
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 240);
    EXPECT_EQ(h.getAssignment()[0], 0);
    EXPECT_EQ(h.getAssignment()[1], 1);
    EXPECT_EQ(h.getAssignment()[2], 2);
    expectScoreConsistent(compat, h);
}

// ── 3-couple non-trivial ──────────────────────────────────────────────────────

// Greedy column-by-column would not find the optimum.
// Optimal: M0→W0(4) + M1→W2(5) + M2→W1(2) = 11.
TEST(HungarianTest, ThreeCouplesNonTrivial) {
    std::vector<std::vector<int>> compat = {
        {4, 1, 3},
        {2, 0, 5},
        {3, 2, 2},
    };
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 11);
    expectScoreConsistent(compat, h);
}

// ── 4-couple ─────────────────────────────────────────────────────────────────

// Known optimum: M0↔W0(85) + M1↔W1(85) + M2↔W2(90) + M3↔W3(95) = 355.
TEST(HungarianTest, FourCouples) {
    std::vector<std::vector<int>> compat = {
        {85, 60, 70, 45},
        {70, 85, 55, 80},
        {45, 70, 90, 60},
        {60, 50, 40, 95},
    };
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 355);
    expectScoreConsistent(compat, h);
}

// ── Uniform compatibility ─────────────────────────────────────────────────────

// All pairs equally compatible — any perfect assignment has the same total.
TEST(HungarianTest, UniformCompatibility) {
    const int n = 3;
    std::vector<std::vector<int>> compat(n, std::vector<int>(n, 50));
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 150);
    expectScoreConsistent(compat, h);
}

// ── Rerun idempotency ─────────────────────────────────────────────────────────

TEST(HungarianTest, RerunIdempotent) {
    std::vector<std::vector<int>> compat = {
        {9, 2, 7, 8},
        {6, 4, 3, 7},
        {5, 8, 1, 8},
        {7, 6, 9, 4},
    };
    Hungarian h(compat);
    h.solve();
    const int score1   = h.getMaxScore();
    const auto assign1 = h.getAssignment();
    h.solve();
    EXPECT_EQ(h.getMaxScore(), score1);
    EXPECT_EQ(h.getAssignment(), assign1);
    expectScoreConsistent(compat, h);
}

// ── n=1 trivial ───────────────────────────────────────────────────────────────

TEST(HungarianTest, N1Trivial) {
    Hungarian h({{42}});
    h.solve();
    EXPECT_EQ(h.getMaxScore(), 42);
    EXPECT_EQ(h.getAssignment()[0], 0);
}

// ── Anti-diagonal optimal ─────────────────────────────────────────────────────

// Greedy (top-left scan) gives 1+1=2; optimal cross-assignment gives 10+10=20.
TEST(HungarianTest, AntiDiagonalOptimal) {
    std::vector<std::vector<int>> compat = {{1, 10}, {10, 1}};
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 20);
    EXPECT_EQ(h.getAssignment()[0], 1);
    EXPECT_EQ(h.getAssignment()[1], 0);
    expectScoreConsistent(compat, h);
}

// ── All-zero matrix ───────────────────────────────────────────────────────────

// Every assignment has score 0; result must still be a valid permutation.
TEST(HungarianTest, AllZerosValidPermutation) {
    const int n = 3;
    std::vector<std::vector<int>> compat(n, std::vector<int>(n, 0));
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 0);
    const auto& a = h.getAssignment();
    std::vector<bool> used(n, false);
    for (int i = 0; i < n; ++i) {
        ASSERT_GE(a[i], 0) << "assignment[" << i << "] is negative";
        ASSERT_LT(a[i], n) << "assignment[" << i << "] out of range";
        EXPECT_FALSE(used[a[i]]) << "column " << a[i] << " assigned twice";
        used[a[i]] = true;
    }
}

// ── 5-couple anti-diagonal ────────────────────────────────────────────────────

// The single high-value entry per row is on the anti-diagonal (value 9).
// A greedy row-scan would collide on column 0; Hungarian finds the unique
// valid anti-diagonal assignment. Optimal score = 5×9 = 45.
TEST(HungarianTest, FiveCouplesAntiDiagonal) {
    std::vector<std::vector<int>> compat = {
        {1, 1, 1, 1, 9},
        {1, 1, 1, 9, 1},
        {1, 1, 9, 1, 1},
        {1, 9, 1, 1, 1},
        {9, 1, 1, 1, 1},
    };
    Hungarian h(compat);
    h.solve();

    EXPECT_EQ(h.getMaxScore(), 45);
    EXPECT_EQ(h.getAssignment()[0], 4);
    EXPECT_EQ(h.getAssignment()[4], 0);
    expectScoreConsistent(compat, h);
}
