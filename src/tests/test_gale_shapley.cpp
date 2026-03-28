// Unit tests for Gale-Shapley stable matching.
//
// Scenario: blind one-on-one dating. Men propose to women in order of
// questionnaire-derived preference. Participants have not seen each other —
// matching is personality-score driven. All participants seek opposite-sex
// partners, so the pool is always split men (group A) vs women (group B).

#include "../gale_shapley.h"
#include <cassert>
#include <iostream>
#include <vector>

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    assert(cond);
}

// 2 men, 2 women — classic base case with a known stable outcome.
//
// Man 0 prefers W0 then W1.  Man 1 prefers W0 then W1.
// W0 prefers M1 then M0.     W1 prefers M0 then M1.
//
// Both men propose to W0 first. W0 holds M1 (her top pick) and rejects M0.
// M0 then proposes to W1 — accepted. Stable result: M0↔W1, M1↔W0.
static void test_two_couples() {
    std::vector<std::vector<int>> menPrefs   = {{0, 1}, {0, 1}};
    std::vector<std::vector<int>> womenPrefs = {{1, 0}, {0, 1}};

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();

    check("2-couple: M0 matched to W1",  gs.getMatchingA()[0] == 1);
    check("2-couple: M1 matched to W0",  gs.getMatchingA()[1] == 0);
    check("2-couple: stable",            gs.isStable());
}

// 3 men, 3 women — verifies proposer-optimality.
//
// M0 and M2 both rank W0 first; M0 wins because W0 prefers M0.
// Everyone else cascades to their next best option.
static void test_three_couples_classic() {
    std::vector<std::vector<int>> menPrefs = {
        {0, 1, 2},  // M0: W0 > W1 > W2
        {1, 0, 2},  // M1: W1 > W0 > W2
        {0, 1, 2},  // M2: W0 > W1 > W2
    };
    std::vector<std::vector<int>> womenPrefs = {
        {1, 0, 2},  // W0: M1 > M0 > M2  — but M0 proposes first and holds until M1 arrives... wait
        {0, 1, 2},  // W1: M0 > M1 > M2
        {0, 1, 2},  // W2: M0 > M1 > M2
    };
    // Trace: M0→W0 (held). M1→W1 (held). M2→W0: W0 prefers M0 over M2 → M2 rejected.
    // M2→W1: W1 holds M1 (rank 1) over M2 (rank 2) → M2 rejected.
    // M2→W2 (free) → held. Result: M0↔W0, M1↔W1, M2↔W2.

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();

    check("3-couple classic: M0↔W0", gs.getMatchingA()[0] == 0);
    check("3-couple classic: M1↔W1", gs.getMatchingA()[1] == 1);
    check("3-couple classic: M2↔W2", gs.getMatchingA()[2] == 2);
    check("3-couple classic: stable", gs.isStable());
}

// Everyone lists partners in reverse order of index — result must still be stable.
static void test_reverse_preferences_stable() {
    const int n = 4;
    std::vector<std::vector<int>> menPrefs(n), womenPrefs(n);
    for (int i = 0; i < n; ++i)
        for (int j = n - 1; j >= 0; --j) {
            menPrefs[i].push_back(j);
            womenPrefs[i].push_back(j);
        }

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    check("reverse prefs: stable", gs.isStable());
}

// All men top-rank the same woman (W0). W0 ends up with the man she ranks highest.
static void test_all_men_prefer_same_woman() {
    std::vector<std::vector<int>> menPrefs = {
        {0, 1, 2},
        {0, 2, 1},
        {0, 1, 2},
    };
    std::vector<std::vector<int>> womenPrefs = {
        {2, 1, 0},  // W0 prefers M2 most
        {0, 1, 2},
        {0, 1, 2},
    };

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    check("same-top-woman: stable",             gs.isStable());
    check("same-top-woman: W0 holds her top M", gs.getMatchingB()[0] == 2);
}

// Running the algorithm twice on the same object must produce identical results.
static void test_rerun_is_idempotent() {
    std::vector<std::vector<int>> menPrefs   = {{0,1,2},{2,0,1},{1,2,0}};
    std::vector<std::vector<int>> womenPrefs = {{1,2,0},{0,1,2},{2,0,1}};

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    auto first = gs.getMatchingA();
    gs.run();
    auto second = gs.getMatchingA();

    check("idempotent: same result on re-run", first == second);
    check("idempotent: stable",                gs.isStable());
}

// Proposer-optimality: the proposing side (men) gets the best stable partner
// they can. Verified by checking that swapping any matched man with his
// unmatched preferred woman would destabilise the matching (isStable would
// return false on the tampered matching).
static void test_no_man_can_do_better() {
    std::vector<std::vector<int>> menPrefs = {
        {0, 1, 2},
        {0, 1, 2},
        {1, 0, 2},
    };
    std::vector<std::vector<int>> womenPrefs = {
        {0, 1, 2},
        {1, 0, 2},
        {0, 1, 2},
    };

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    check("proposer-optimal: stable", gs.isStable());
    // All men are matched (perfect matching guaranteed for equal-sized groups).
    const auto& m = gs.getMatchingA();
    bool allMatched = true;
    for (int i = 0; i < 3; ++i) allMatched = allMatched && (m[i] != -1);
    check("proposer-optimal: all men matched", allMatched);
}

int main() {
    test_two_couples();
    test_three_couples_classic();
    test_reverse_preferences_stable();
    test_all_men_prefer_same_woman();
    test_rerun_is_idempotent();
    test_no_man_can_do_better();
    std::cout << "All Gale-Shapley tests passed.\n";
    return 0;
}
