// GTest suite for Gale-Shapley stable matching.
//
// Scenario: blind one-on-one dating. Men propose to women in order of
// questionnaire-derived preference. Participants have not seen each other —
// matching is personality-score driven. All participants seek opposite-sex
// partners, so the pool is always split men (group A) vs women (group B).

#include "../gale_shapley.h"

#include <gtest/gtest.h>
#include <vector>

// ── 2-couple classic ──────────────────────────────────────────────────────────

// Man 0 prefers W0 then W1.  Man 1 prefers W0 then W1.
// W0 prefers M1 then M0.     W1 prefers M0 then M1.
//
// Both men propose to W0 first. W0 holds M1 (her top pick) and rejects M0.
// M0 then proposes to W1 — accepted. Stable result: M0↔W1, M1↔W0.
TEST(GaleShapleyTest, TwoCouplesClassic) {
    std::vector<std::vector<int>> menPrefs   = {{0, 1}, {0, 1}};
    std::vector<std::vector<int>> womenPrefs = {{1, 0}, {0, 1}};

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();

    EXPECT_EQ(gs.getMatchingA()[0], 1);
    EXPECT_EQ(gs.getMatchingA()[1], 0);
    EXPECT_TRUE(gs.isStable());
}

// ── 3-couple classic ──────────────────────────────────────────────────────────

// M0 and M2 both rank W0 first; M0 wins because W0 prefers M0 over M2.
// Everyone else cascades to their next best option.
TEST(GaleShapleyTest, ThreeCouplesClassic) {
    std::vector<std::vector<int>> menPrefs = {
        {0, 1, 2},  // M0: W0 > W1 > W2
        {1, 0, 2},  // M1: W1 > W0 > W2
        {0, 1, 2},  // M2: W0 > W1 > W2
    };
    std::vector<std::vector<int>> womenPrefs = {
        {1, 0, 2},  // W0: M1 > M0 > M2
        {0, 1, 2},  // W1: M0 > M1 > M2
        {0, 1, 2},  // W2: M0 > M1 > M2
    };

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();

    EXPECT_EQ(gs.getMatchingA()[0], 0);
    EXPECT_EQ(gs.getMatchingA()[1], 1);
    EXPECT_EQ(gs.getMatchingA()[2], 2);
    EXPECT_TRUE(gs.isStable());
}

// ── Reverse preferences ───────────────────────────────────────────────────────

// Everyone lists partners in reverse index order — result must still be stable.
TEST(GaleShapleyTest, ReversePreferencesStable) {
    const int n = 4;
    std::vector<std::vector<int>> menPrefs(n), womenPrefs(n);
    for (int i = 0; i < n; ++i)
        for (int j = n - 1; j >= 0; --j) {
            menPrefs[i].push_back(j);
            womenPrefs[i].push_back(j);
        }

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    EXPECT_TRUE(gs.isStable());
}

// ── All men prefer same woman ─────────────────────────────────────────────────

// All men top-rank W0. W0 ends up with the man she ranks highest.
TEST(GaleShapleyTest, AllMenPreferSameWoman) {
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
    EXPECT_TRUE(gs.isStable());
    EXPECT_EQ(gs.getMatchingB()[0], 2);  // W0 holds her top choice M2
}

// ── Idempotency ───────────────────────────────────────────────────────────────

// Running the algorithm twice on the same object must produce identical results.
TEST(GaleShapleyTest, RerunIsIdempotent) {
    std::vector<std::vector<int>> menPrefs   = {{0,1,2},{2,0,1},{1,2,0}};
    std::vector<std::vector<int>> womenPrefs = {{1,2,0},{0,1,2},{2,0,1}};

    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    const auto first = gs.getMatchingA();
    gs.run();
    const auto second = gs.getMatchingA();

    EXPECT_EQ(first, second);
    EXPECT_TRUE(gs.isStable());
}

// ── Proposer-optimality ───────────────────────────────────────────────────────

// The proposing side (men) gets the best stable partner they can; all men
// must be matched in a same-sized group.
TEST(GaleShapleyTest, NoManCanDoBetter) {
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
    EXPECT_TRUE(gs.isStable());

    const auto& m = gs.getMatchingA();
    for (int i = 0; i < 3; ++i)
        EXPECT_NE(m[i], -1) << "M" << i << " should be matched";
}

// ── n=1 ───────────────────────────────────────────────────────────────────────

// Single man, single woman — always paired.
TEST(GaleShapleyTest, N1Trivial) {
    GaleShapley gs({{0}}, {{0}});
    gs.run();
    EXPECT_EQ(gs.getMatchingA()[0], 0);
    EXPECT_EQ(gs.getMatchingB()[0], 0);
    EXPECT_TRUE(gs.isStable());
}

// ── Symmetric top preferences ─────────────────────────────────────────────────

// Every person's first choice is their "mirror" partner. Every first proposal
// is immediately accepted; result must be the identity permutation.
TEST(GaleShapleyTest, SymmetricTopPreferencesGiveIdentity) {
    std::vector<std::vector<int>> menPrefs = {
        {0, 1, 2, 3},
        {1, 0, 2, 3},
        {2, 0, 1, 3},
        {3, 0, 1, 2},
    };
    std::vector<std::vector<int>> womenPrefs = {
        {0, 1, 2, 3},
        {1, 0, 2, 3},
        {2, 0, 1, 3},
        {3, 0, 1, 2},
    };
    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    const auto& m = gs.getMatchingA();
    EXPECT_EQ(m[0], 0);
    EXPECT_EQ(m[1], 1);
    EXPECT_EQ(m[2], 2);
    EXPECT_EQ(m[3], 3);
    EXPECT_TRUE(gs.isStable());
}

// ── Receiver-pessimality ──────────────────────────────────────────────────────

// Two stable matchings exist:
//   S1 (men-optimal):   M0↔W0, M1↔W1   ← GS must produce this
//   S2 (women-optimal): M0↔W1, M1↔W0
//
// M0: W0>W1   M1: W1>W0   W0: M1>M0   W1: M0>M1
//
// Women get their worst stable partner — the receiver-pessimality theorem.
TEST(GaleShapleyTest, ReceiverGetsWorstStablePartner) {
    std::vector<std::vector<int>> menPrefs   = {{0, 1}, {1, 0}};
    std::vector<std::vector<int>> womenPrefs = {{1, 0}, {0, 1}};
    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    EXPECT_EQ(gs.getMatchingA()[0], 0);
    EXPECT_EQ(gs.getMatchingA()[1], 1);
    EXPECT_EQ(gs.getMatchingB()[0], 0);  // W0 holds M0, her worst stable match
    EXPECT_TRUE(gs.isStable());
}

// ── Cascading proposals ───────────────────────────────────────────────────────

// 5 men all share identical rankings (W0>W1>…>W4). Each W_i uniquely prefers
// M_i first, so M_i cascades exactly to W_i. The diagonal is the unique stable
// outcome.
TEST(GaleShapleyTest, AllMenSamePrefsCascading) {
    const int n = 5;
    std::vector<std::vector<int>> menPrefs(n, {0, 1, 2, 3, 4});
    std::vector<std::vector<int>> womenPrefs(n);
    for (int w = 0; w < n; ++w) {
        womenPrefs[w].push_back(w);
        for (int m = 0; m < n; ++m)
            if (m != w) womenPrefs[w].push_back(m);
    }
    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    const auto& m = gs.getMatchingA();
    for (int i = 0; i < n; ++i)
        EXPECT_EQ(m[i], i) << "M" << i << " should be matched to W" << i;
    EXPECT_TRUE(gs.isStable());
}

// ── matchA / matchB consistency ───────────────────────────────────────────────

// matchB must be the inverse of matchA: if matchA[i]==j then matchB[j]==i.
TEST(GaleShapleyTest, MatchingAAndBAreMutualInverses) {
    std::vector<std::vector<int>> menPrefs   = {{1, 0, 2}, {0, 2, 1}, {2, 1, 0}};
    std::vector<std::vector<int>> womenPrefs = {{2, 0, 1}, {0, 1, 2}, {1, 2, 0}};
    GaleShapley gs(menPrefs, womenPrefs);
    gs.run();
    const auto& a = gs.getMatchingA();
    const auto& b = gs.getMatchingB();
    for (int i = 0; i < 3; ++i)
        if (a[i] != -1)
            EXPECT_EQ(b[a[i]], i) << "matchB[matchA[" << i << "]] != " << i;
    EXPECT_TRUE(gs.isStable());
}
