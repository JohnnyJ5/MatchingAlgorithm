// Unit tests for Edmonds' Blossom algorithm (general graph maximum matching).
//
// Scenario: blind one-on-one dating. The compatibility graph is treated as a
// general undirected graph. Edges are added only between men and women
// (bipartite structure) — the algorithm enforces no such constraint itself,
// but the dating requirement means we never add a man–man or woman–woman edge.
// Men are assigned vertex indices 0..n-1; women are n..2n-1.
// The algorithm maximises the number of couples formed.

#include "../blossom.h"
#include <cassert>
#include <iostream>
#include <vector>

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    assert(cond);
}

// Validates that the matching vector is self-consistent (symmetric and valid).
static void assertValidMatching(const std::vector<int>& m) {
    int n = static_cast<int>(m.size());
    for (int i = 0; i < n; ++i) {
        if (m[i] == -1) continue;
        assert(m[i] != i);
        assert(m[m[i]] == i);
    }
}

// 2 men, 2 women — one compatible pair each: perfect matching.
static void test_two_couples_perfect() {
    // M0=0, M1=1, W0=2, W1=3
    Blossom b(4);
    b.addCompatiblePair(0, 2);  // M0↔W0
    b.addCompatiblePair(1, 3);  // M1↔W1

    int couples = b.maxMatching();
    check("2-couple perfect: 2 matches", couples == 2);
    assertValidMatching(b.getMatching());
    check("2-couple perfect: valid", true);
}

// No compatible pairs at all — nobody matched.
static void test_no_compatible_pairs() {
    Blossom b(6);  // 3 men + 3 women, no edges
    check("no-compat: 0 matches", b.maxMatching() == 0);
}

// 3 men, 3 women — all men compatible with all women: perfect matching of 3.
static void test_fully_compatible_pool() {
    // Men: 0,1,2   Women: 3,4,5
    Blossom b(6);
    for (int m = 0; m < 3; ++m)
        for (int w = 3; w < 6; ++w)
            b.addCompatiblePair(m, w);

    int couples = b.maxMatching();
    check("full-compat: 3 couples", couples == 3);
    assertValidMatching(b.getMatching());
    check("full-compat: valid", true);
}

// Bottleneck: two men compete for the same woman; one gets her, the other is
// matched to his second compatible option.
static void test_shared_top_choice() {
    // M0=0, M1=1, M2=2   W0=3, W1=4
    // M0 compatible with W0 only.
    // M1 compatible with W0 and W1.
    // M2 compatible with W1 only.
    // Optimal: M0↔W0, M2↔W1, M1 unmatched  OR  M1↔W0, M2↔W1, M0 unmatched.
    // Max = 2.
    Blossom b(5);
    b.addCompatiblePair(0, 3);
    b.addCompatiblePair(1, 3);
    b.addCompatiblePair(1, 4);
    b.addCompatiblePair(2, 4);

    int couples = b.maxMatching();
    check("shared-top: 2 couples (max possible)", couples == 2);
    assertValidMatching(b.getMatching());
    check("shared-top: valid", true);
}

// More women than men — all men matched, extra women unmatched.
static void test_more_women_than_men() {
    // 3 men (0,1,2), 5 women (3,4,5,6,7)
    Blossom b(8);
    b.addCompatiblePair(0, 3);
    b.addCompatiblePair(1, 5);
    b.addCompatiblePair(2, 7);

    int couples = b.maxMatching();
    check("more-women: all 3 men matched", couples == 3);
    assertValidMatching(b.getMatching());
    check("more-women: valid", true);
}

// 4 men, 4 women — chain of compatible pairs requires augmenting paths.
//
// M0↔W0, M1↔W0 and W1, M2↔W1 and W2, M3↔W2 and W3.
// Greedy may match M0↔W0 then get stuck; augmenting paths find 4 couples.
static void test_augmenting_path_required() {
    // Men: 0,1,2,3   Women: 4,5,6,7
    Blossom b(8);
    b.addCompatiblePair(0, 4);
    b.addCompatiblePair(1, 4);
    b.addCompatiblePair(1, 5);
    b.addCompatiblePair(2, 5);
    b.addCompatiblePair(2, 6);
    b.addCompatiblePair(3, 6);
    b.addCompatiblePair(3, 7);

    int couples = b.maxMatching();
    check("augmenting: 4 couples", couples == 4);
    assertValidMatching(b.getMatching());
    check("augmenting: valid", true);
}

// Single compatible pair — exactly one couple.
static void test_single_compatible_pair() {
    Blossom b(2);
    b.addCompatiblePair(0, 1);
    check("single pair: 1 couple", b.maxMatching() == 1);
}

int main() {
    test_two_couples_perfect();
    test_no_compatible_pairs();
    test_fully_compatible_pool();
    test_shared_top_choice();
    test_more_women_than_men();
    test_augmenting_path_required();
    test_single_compatible_pair();
    std::cout << "All Blossom tests passed.\n";
    return 0;
}
