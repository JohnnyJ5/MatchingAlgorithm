#pragma once
#include <vector>

// Hopcroft-Karp maximum bipartite matching.
//
// Left vertices: 0 .. leftSize-1
// Right vertices: 0 .. rightSize-1
//
// Runs in O(E * sqrt(V)).
class HopcroftKarp {
public:
    HopcroftKarp(int leftSize, int rightSize);

    // Add a compatible edge between left vertex a and right vertex b.
    void addCompatiblePair(int a, int b);

    // Compute and return the size of the maximum matching.
    int maxMatching();

    // After maxMatching():  matchLeft[a] = b, or -1 if unmatched.
    const std::vector<int>& getMatching() const { return matchLeft_; }

private:
    static constexpr int INF = 1e9;

    int left_, right_;
    std::vector<std::vector<int>> adj_;   // adjacency list for left vertices

    std::vector<int> matchLeft_;   // matchLeft[a]  = b or -1
    std::vector<int> matchRight_;  // matchRight[b] = a or -1
    std::vector<int> distLeft_;    // BFS distance for left vertices

    bool bfs();
    bool dfs(int a);
};
