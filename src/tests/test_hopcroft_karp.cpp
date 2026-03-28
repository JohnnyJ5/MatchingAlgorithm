// Unit tests for Hopcroft-Karp maximum bipartite matching.
//
// Scenario: blind one-on-one dating with a compatibility threshold. Two people
// are only paired as candidates if their questionnaire scores exceed the
// threshold — otherwise they are considered incompatible and no edge is added.
// Men occupy the left side of the bipartite graph; women the right side.
// The algorithm maximises the number of couples formed.

#include "../hopcroft_karp.h"
#include <cassert>
#include <iostream>
#include <vector>

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    assert(cond);
}

// Validates that no two men are matched to the same woman.
static void assertValidMatching(const std::vector<int>& matchLeft, int rightSize) {
    std::vector<bool> used(rightSize, false);
    for (int w : matchLeft) {
        if (w == -1) continue;
        assert(w >= 0 && w < rightSize);
        assert(!used[w]);
        used[w] = true;
    }
}

// Every man is compatible with exactly one woman (diagonal) — perfect matching.
static void test_perfect_matching() {
    HopcroftKarp hk(4, 4);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 2);
    hk.addCompatiblePair(3, 3);

    int couples = hk.maxMatching();
    check("perfect: 4 couples formed", couples == 4);
    assertValidMatching(hk.getMatching(), 4);
    check("perfect: valid assignment", true);
}

// No man passes the compatibility threshold with any woman — nobody matched.
static void test_no_compatible_pairs() {
    HopcroftKarp hk(4, 4);
    // No edges added — all scores below threshold.
    check("no-compat: 0 couples", hk.maxMatching() == 0);
}

// Partial compatibility: some men share a top compatible woman, forcing the
// algorithm to reroute one of them to his second compatible option.
//
//  M0 compatible with W0 only.
//  M1 compatible with W0 and W1.
//  M2 compatible with W1 only.
//  Maximum matching = 2 (not 3, since there is no W2).
static void test_partial_compatibility() {
    HopcroftKarp hk(3, 2);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 1);

    int couples = hk.maxMatching();
    check("partial: 2 couples (max possible)", couples == 2);
    assertValidMatching(hk.getMatching(), 2);
    check("partial: valid assignment", true);
}

// All men are compatible with all women (everyone above threshold) — perfect match.
static void test_fully_compatible_pool() {
    const int n = 5;
    HopcroftKarp hk(n, n);
    for (int m = 0; m < n; ++m)
        for (int w = 0; w < n; ++w)
            hk.addCompatiblePair(m, w);

    int couples = hk.maxMatching();
    check("full-compat: all 5 coupled", couples == n);
    assertValidMatching(hk.getMatching(), n);
    check("full-compat: valid assignment", true);
}

// More women than men — all men must be matched, some women left unmatched.
static void test_more_women_than_men() {
    // 3 men, 5 women; each man compatible with a distinct woman.
    HopcroftKarp hk(3, 5);
    hk.addCompatiblePair(0, 1);
    hk.addCompatiblePair(1, 3);
    hk.addCompatiblePair(2, 4);

    int couples = hk.maxMatching();
    check("more-women: all 3 men matched", couples == 3);
}

// Augmenting path required: initial greedy assignment must be improved.
//
//  M0→W0 (greedy). M1→W0 blocked, tries W1. M2→W1 blocked.
//  Augmenting path: M2→W1←M1→W0←M0  — reallocate to match all 3.
//  Wait: M0 has only W0. M1 has W0, W1. M2 has W1.
//  Optimal: M0↔W0, M1↔W1, M2 unmatched  OR  M1↔W0, M2↔W1, M0 unmatched.
//  Max = 2 (M2 has no other option once W1 is taken).
//  Augmenting path resolves to 2 couples.
static void test_augmenting_path_needed() {
    HopcroftKarp hk(3, 2);
    hk.addCompatiblePair(0, 0);
    hk.addCompatiblePair(1, 0);
    hk.addCompatiblePair(1, 1);
    hk.addCompatiblePair(2, 1);

    check("augmenting: 2 couples", hk.maxMatching() == 2);
}

int main() {
    test_perfect_matching();
    test_no_compatible_pairs();
    test_partial_compatibility();
    test_fully_compatible_pool();
    test_more_women_than_men();
    test_augmenting_path_needed();
    std::cout << "All Hopcroft-Karp tests passed.\n";
    return 0;
}
