// Unit tests for the Hungarian algorithm (optimal bipartite assignment).
//
// Scenario: blind one-on-one dating. Every man must be assigned to exactly one
// woman. The compatibility score between each pair is derived from a
// questionnaire. The Hungarian algorithm finds the assignment that maximises
// the *total* combined compatibility score across all couples — the globally
// optimal outcome.

#include "../hungarian.h"
#include <cassert>
#include <iostream>
#include <vector>

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    assert(cond);
}

// Verifies that the reported score equals the sum of assigned pair scores.
static void assertScoreConsistent(const std::vector<std::vector<int>>& mat,
                                   const Hungarian& h) {
    int total = 0;
    const auto& a = h.getAssignment();
    for (int i = 0; i < static_cast<int>(a.size()); ++i)
        total += mat[i][a[i]];
    assert(total == h.getMaxScore());
}

// 2×2: each man is perfectly compatible only with one woman.
// Optimal is obvious — verify the algorithm picks it.
static void test_two_couples_clear_winner() {
    std::vector<std::vector<int>> compat = {
        {90, 10},   // M0: great with W0, poor with W1
        {10, 90},   // M1: poor with W0, great with W1
    };
    Hungarian h(compat);
    h.solve();

    check("2-couple optimal: score 180",    h.getMaxScore() == 180);
    check("2-couple optimal: M0→W0",        h.getAssignment()[0] == 0);
    check("2-couple optimal: M1→W1",        h.getAssignment()[1] == 1);
    assertScoreConsistent(compat, h);
    check("2-couple optimal: score consistent", true);
}

// 3×3: diagonal dominates — algorithm should assign each man to his best match.
static void test_three_couples_diagonal() {
    std::vector<std::vector<int>> compat = {
        {80,  5,  5},
        { 5, 80,  5},
        { 5,  5, 80},
    };
    Hungarian h(compat);
    h.solve();

    check("3-couple diagonal: score 240",   h.getMaxScore() == 240);
    check("3-couple diagonal: M0→W0",       h.getAssignment()[0] == 0);
    check("3-couple diagonal: M1→W1",       h.getAssignment()[1] == 1);
    check("3-couple diagonal: M2→W2",       h.getAssignment()[2] == 2);
    assertScoreConsistent(compat, h);
    check("3-couple diagonal: consistent",  true);
}

// 3×3: non-trivial — greedy column-by-column would not find the optimum.
// Known optimum: M0→W0(4) + M1→W2(5) + M2→W1(2) = 11.
static void test_three_couples_nontrivial() {
    std::vector<std::vector<int>> compat = {
        {4, 1, 3},
        {2, 0, 5},
        {3, 2, 2},
    };
    Hungarian h(compat);
    h.solve();

    check("3-couple non-trivial: score 11", h.getMaxScore() == 11);
    assertScoreConsistent(compat, h);
    check("3-couple non-trivial: consistent", true);
}

// 4×4: larger pool — validates correctness at scale and that reported score
// matches the actual sum of assigned pairs.
static void test_four_couples() {
    std::vector<std::vector<int>> compat = {
        {85, 60, 70, 45},
        {70, 85, 55, 80},
        {45, 70, 90, 60},
        {60, 50, 40, 95},
    };
    Hungarian h(compat);
    h.solve();

    // Known optimum: M0↔W0(85) + M1↔W1(85) + M2↔W2(90) + M3↔W3(95) = 355.
    check("4-couple: score 355", h.getMaxScore() == 355);
    assertScoreConsistent(compat, h);
    check("4-couple: consistent", true);
}

// Uniform scores: all pairs equally compatible — any perfect assignment has
// the same total score.
static void test_uniform_compatibility() {
    const int n = 3;
    std::vector<std::vector<int>> compat(n, std::vector<int>(n, 50));
    Hungarian h(compat);
    h.solve();

    check("uniform: score 150",   h.getMaxScore() == 150);
    assertScoreConsistent(compat, h);
    check("uniform: consistent",  true);
}

// Re-running solve() twice must return the same assignment and score.
static void test_rerun_idempotent() {
    std::vector<std::vector<int>> compat = {
        {9, 2, 7, 8},
        {6, 4, 3, 7},
        {5, 8, 1, 8},
        {7, 6, 9, 4},
    };
    Hungarian h(compat);
    h.solve();
    int score1 = h.getMaxScore();
    auto assign1 = h.getAssignment();
    h.solve();
    int score2 = h.getMaxScore();
    auto assign2 = h.getAssignment();

    check("re-run: same score",      score1 == score2);
    check("re-run: same assignment", assign1 == assign2);
    assertScoreConsistent(compat, h);
    check("re-run: consistent",      true);
}

int main() {
    test_two_couples_clear_winner();
    test_three_couples_diagonal();
    test_three_couples_nontrivial();
    test_four_couples();
    test_uniform_compatibility();
    test_rerun_idempotent();
    std::cout << "All Hungarian tests passed.\n";
    return 0;
}
