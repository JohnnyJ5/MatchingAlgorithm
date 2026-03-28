// Blind Dating Algorithm Analysis
//
// Scenario: a pool of men and women submit questionnaire answers. A
// compatibility score (0–100) is computed for every man–woman pair. Crucially,
// participants have NOT seen each other — matching is driven entirely by
// personality and values, not appearance. All participants seek an
// opposite-sex partner, so the problem is always bipartite.
//
// Three algorithms are applied to the same dataset and their results compared:
//
//   1. Gale-Shapley  — stable matching; no couple would mutually prefer each
//                      other over their assigned partners.
//   2. Hopcroft-Karp — maximum pairing; counts compatible pairs (score >=
//                      threshold) as edges, then maximises the number of
//                      couples formed.
//   3. Hungarian     — optimal assignment; every man is assigned exactly one
//                      woman and the total compatibility score is maximised.
//   4. Blossom       — general-graph maximum matching; same threshold edges as
//                      Hopcroft-Karp but solved via Edmonds' algorithm.

#include "src/gale_shapley.h"
#include "src/hopcroft_karp.h"
#include "src/hungarian.h"
#include "src/blossom.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

// ── Dataset ───────────────────────────────────────────────────────────────────

static const std::vector<std::string> MEN   = {"Aaron", "Ben", "Carlos", "Dan", "Evan", "Frank"};
static const std::vector<std::string> WOMEN = {"Amy",   "Beth", "Clara",  "Dana", "Elena", "Fiona"};
static const int N = 6;

// Compatibility threshold used by Hopcroft-Karp and Blossom.
static const int THRESHOLD = 65;

// COMPAT[m][w] = questionnaire compatibility score between man m and woman w.
static const std::vector<std::vector<int>> COMPAT = {
    //        Amy  Beth  Clara  Dana  Elena  Fiona
    /* Aaron  */ {85,   60,   70,   45,   90,   55},
    /* Ben    */ {70,   85,   55,   80,   40,   65},
    /* Carlos */ {45,   70,   90,   60,   75,   50},
    /* Dan    */ {60,   50,   40,   95,   65,   80},
    /* Evan   */ {90,   40,   65,   55,   50,   70},
    /* Frank  */ {55,   75,   80,   70,   60,   85},
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static void separator() { std::cout << std::string(56, '-') << "\n"; }

// Print a matched couple list and summary statistics.
// matchM[m] = w index, or -1 if unmatched.
static void printResults(const std::vector<int>& matchM) {
    int total = 0, matched = 0;
    for (int m = 0; m < N; ++m) {
        int w = matchM[m];
        if (w == -1) {
            std::cout << "  " << std::left << std::setw(8) << MEN[m]
                      << "  ->  unmatched\n";
        } else {
            int s = COMPAT[m][w];
            total += s;
            ++matched;
            std::cout << "  " << std::left << std::setw(8) << MEN[m]
                      << "  <->  " << std::setw(7) << WOMEN[w]
                      << "  score: " << s << "\n";
        }
    }
    std::cout << "\n";
    std::cout << "  Couples   : " << matched << " / " << N << "\n";
    if (matched > 0)
        std::cout << "  Avg score : " << std::fixed << std::setprecision(1)
                  << static_cast<double>(total) / matched << "\n";
    std::cout << "  Total     : " << total << "\n";
}

// Build preference lists for each side ordered by descending compatibility.
static std::vector<std::vector<int>> menPrefs() {
    std::vector<std::vector<int>> prefs(N);
    for (int m = 0; m < N; ++m) {
        prefs[m].resize(N);
        std::iota(prefs[m].begin(), prefs[m].end(), 0);
        std::sort(prefs[m].begin(), prefs[m].end(),
                  [&](int a, int b) { return COMPAT[m][a] > COMPAT[m][b]; });
    }
    return prefs;
}

static std::vector<std::vector<int>> womenPrefs() {
    std::vector<std::vector<int>> prefs(N);
    for (int w = 0; w < N; ++w) {
        prefs[w].resize(N);
        std::iota(prefs[w].begin(), prefs[w].end(), 0);
        std::sort(prefs[w].begin(), prefs[w].end(),
                  [&](int a, int b) { return COMPAT[a][w] > COMPAT[b][w]; });
    }
    return prefs;
}

// ── Algorithm runners ─────────────────────────────────────────────────────────

static std::vector<int> runGaleShapley() {
    GaleShapley gs(menPrefs(), womenPrefs());
    gs.run();

    std::cout << "  Men propose in descending-score order; women accept or trade up.\n";
    std::cout << "  Guarantee: no two unmatched people both prefer each other (stable).\n\n";

    auto matchM = gs.getMatchingA();
    printResults(matchM);
    std::cout << "  Stable    : " << (gs.isStable() ? "yes" : "no") << "\n";
    return matchM;
}

static std::vector<int> runHopcroftKarp() {
    HopcroftKarp hk(N, N);
    for (int m = 0; m < N; ++m)
        for (int w = 0; w < N; ++w)
            if (COMPAT[m][w] >= THRESHOLD)
                hk.addCompatiblePair(m, w);

    std::cout << "  Edge added only when score >= " << THRESHOLD << ".\n";
    std::cout << "  Guarantee: maximum number of couples from the compatible-pair graph.\n\n";

    hk.maxMatching();
    auto matchM = hk.getMatching();
    printResults(matchM);
    return matchM;
}

static std::vector<int> runHungarian() {
    Hungarian hung(COMPAT);
    hung.solve();

    std::cout << "  Every man assigned to exactly one woman.\n";
    std::cout << "  Guarantee: globally optimal total compatibility score.\n\n";

    auto matchM = hung.getAssignment();
    printResults(matchM);
    std::cout << "  Optimal   : yes\n";
    return matchM;
}

static std::vector<int> runBlossom() {
    // Men = vertices 0..N-1, Women = vertices N..2N-1.
    Blossom bl(2 * N);
    for (int m = 0; m < N; ++m)
        for (int w = 0; w < N; ++w)
            if (COMPAT[m][w] >= THRESHOLD)
                bl.addCompatiblePair(m, N + w);

    std::cout << "  Treats the dating pool as an undirected graph (no bipartite assumption).\n";
    std::cout << "  Edges only between men and women (score >= " << THRESHOLD << ").\n";
    std::cout << "  Guarantee: maximum matching via augmenting-path search with blossom contraction.\n\n";

    bl.maxMatching();
    const auto& raw = bl.getMatching();

    std::vector<int> matchM(N, -1);
    for (int m = 0; m < N; ++m)
        if (raw[m] != -1)
            matchM[m] = raw[m] - N;

    printResults(matchM);
    return matchM;
}

// ── Comparative analysis ──────────────────────────────────────────────────────

static void analysis(const std::vector<int>& gs,
                     const std::vector<int>& hk,
                     const std::vector<int>& hu,
                     const std::vector<int>& bl) {
    auto score = [](const std::vector<int>& m) {
        int t = 0;
        for (int i = 0; i < N; ++i)
            if (m[i] != -1) t += COMPAT[i][m[i]];
        return t;
    };
    auto count = [](const std::vector<int>& m) {
        return static_cast<int>(std::count_if(m.begin(), m.end(),
                                              [](int w) { return w != -1; }));
    };
    auto same = [](const std::vector<int>& a, const std::vector<int>& b) {
        return a == b;
    };

    std::cout << "\n  Algorithm       Couples  Total score  Avg score\n";
    std::cout << "  --------------- -------- ------------ ---------\n";

    auto row = [&](const char* name, const std::vector<int>& m) {
        int c = count(m), s = score(m);
        std::cout << "  " << std::left << std::setw(15) << name
                  << std::right
                  << std::setw(8)  << c
                  << std::setw(13) << s
                  << std::setw(10) << std::fixed << std::setprecision(1)
                  << (c > 0 ? static_cast<double>(s) / c : 0.0) << "\n";
    };

    row("Gale-Shapley",  gs);
    row("Hopcroft-Karp", hk);
    row("Hungarian",     hu);
    row("Blossom",       bl);

    std::cout << "\n  Notes:\n";
    if (same(hk, bl))
        std::cout << "  * Hopcroft-Karp and Blossom produced identical pairings (expected\n"
                  << "    for a bipartite graph — both find maximum matching).\n";
    if (count(hk) == count(hu))
        std::cout << "  * All threshold-based algorithms matched the full pool.\n";
    else
        std::cout << "  * Hungarian always matches all " << N << " couples (no threshold).\n"
                  << "    Threshold algorithms matched fewer due to score < " << THRESHOLD << " edges.\n";
    if (score(hu) >= score(gs) && score(hu) >= score(hk))
        std::cout << "  * Hungarian achieved the highest total score, as guaranteed by\n"
                  << "    its global-optimality property.\n";
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    separator();
    std::cout << "  Blind Dating Algorithm Analysis\n";
    std::cout << "  " << N << " men  x  " << N << " women  |  "
              << "compatibility threshold: " << THRESHOLD << "\n";
    separator();

    std::cout << "\n--- Algorithm 1: Gale-Shapley (Stable Matching) ---\n";
    auto gsMatch = runGaleShapley();

    separator();
    std::cout << "\n--- Algorithm 2: Hopcroft-Karp (Maximum Pairing) ---\n";
    auto hkMatch = runHopcroftKarp();

    separator();
    std::cout << "\n--- Algorithm 3: Hungarian (Optimal Assignment) ---\n";
    auto huMatch = runHungarian();

    separator();
    std::cout << "\n--- Algorithm 4: Blossom (General Graph Matching) ---\n";
    auto blMatch = runBlossom();

    separator();
    std::cout << "\n=== Comparative Analysis ===\n";
    analysis(gsMatch, hkMatch, huMatch, blMatch);

    std::cout << "\n";
    separator();
    return 0;
}
