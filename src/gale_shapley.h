#pragma once
#include <vector>

// Gale-Shapley stable matching (proposer-optimal).
//
// Both groups must be the same size n.
// prefs[i] is the strictly ordered preference list for person i,
// where each entry is an index into the other group.
class GaleShapley {
public:
    GaleShapley(const std::vector<std::vector<int>>& groupAPrefs,
                const std::vector<std::vector<int>>& groupBPrefs);

    // Run the algorithm. Safe to call multiple times (re-runs from scratch).
    void run();

    // Returns the matching from group A's perspective:
    //   matchA[i] = j  means person i in A is matched with person j in B.
    const std::vector<int>& getMatchingA() const { return matchA_; }

    // Returns the matching from group B's perspective:
    //   matchB[j] = i  means person j in B is matched with person i in A.
    const std::vector<int>& getMatchingB() const { return matchB_; }

    // Convenience alias used in the README API.
    const std::vector<int>& getMatching() const { return matchA_; }

    // Returns true iff the current matching is stable.
    bool isStable() const;

private:
    int n_;
    std::vector<std::vector<int>> prefA_;  // group A preferences
    std::vector<std::vector<int>> rankB_;  // group B: rank[j][i] = rank of A-person i for B-person j

    std::vector<int> matchA_;
    std::vector<int> matchB_;
};
