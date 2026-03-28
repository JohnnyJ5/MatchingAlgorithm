#pragma once
#include <vector>

// Edmonds' Blossom Algorithm — maximum cardinality matching on a general graph.
//
// Vertices: 0 .. numPeople-1  (no sides — anyone can match anyone)
// Time: O(V³)   Space: O(V + E)
class Blossom {
public:
    explicit Blossom(int numPeople);

    // Add a compatibility edge between person a and person b (undirected).
    void addCompatiblePair(int a, int b);

    // Compute and return the maximum number of matched couples.
    int maxMatching();

    // After maxMatching():
    //   match[i] = j  means person i is matched with person j.
    //   match[i] = -1 means person i is unmatched.
    const std::vector<int>& getMatching() const { return match_; }

private:
    int n_;
    std::vector<std::vector<int>> adj_;
    std::vector<int> match_;

    // Augment along a path found from source vertex s.
    // Returns true if an augmenting path was found.
    bool augment(int s);

    // Find the root of vertex v in the union-find structure used for blossom
    // contraction.
    int findRoot(std::vector<int>& parent, int v);
};
