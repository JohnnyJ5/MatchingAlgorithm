#include "blossom.h"
#include <cassert>
#include <queue>
#include <algorithm>

Blossom::Blossom(int numPeople)
    : n_(numPeople), adj_(numPeople), match_(numPeople, -1)
{}

void Blossom::addCompatiblePair(int a, int b) {
    assert(a >= 0 && a < n_);
    assert(b >= 0 && b < n_);
    assert(a != b);
    adj_[a].push_back(b);
    adj_[b].push_back(a);
}

int Blossom::findRoot(std::vector<int>& parent, int v) {
    while (parent[v] != v) {
        parent[v] = parent[parent[v]];  // path compression
        v = parent[v];
    }
    return v;
}

// Gabow's simple O(V·E) blossom augmentation.
// For each free vertex, we run a single BFS that:
//  - labels vertices as EVEN (reachable via alternating path of even length from s)
//    or ODD
//  - contracts blossoms on the fly using a union-find over base[] pointers
bool Blossom::augment(int s) {
    // label: 0 = unlabelled, 1 = even (outer), -1 = odd (inner)
    std::vector<int> label(n_, 0);
    std::vector<int> pred(n_, -1);   // alternating path predecessor
    std::vector<int> base(n_);
    for (int i = 0; i < n_; ++i) base[i] = i;

    label[s] = 1;
    std::queue<int> q;
    q.push(s);

    auto lca = [&](int a, int b) -> int {
        // Find lowest common ancestor of a and b in the alternating tree
        // (used during blossom contraction).
        std::vector<bool> visited(n_, false);
        while (true) {
            a = base[a];
            visited[a] = true;
            if (a == s) break;
            a = base[pred[match_[a]]];
        }
        while (true) {
            b = base[b];
            if (visited[b]) return b;
            b = base[pred[match_[b]]];
        }
    };

    // Contract blossom rooted at r, covering path from v back to r.
    auto markPath = [&](int v, int b, int child) {
        while (base[v] != b) {
            base[v] = b;
            base[match_[v]] = b;
            int prev = pred[v];
            pred[v] = child;
            child = match_[v];
            v = prev;
            if (label[v] != 1) {
                label[v] = 1;
                q.push(v);
            }
        }
    };

    while (!q.empty()) {
        int v = q.front(); q.pop();

        for (int u : adj_[v]) {
            if (base[v] == base[u] || label[u] == -1) continue;

            if (label[u] == 0) {
                // u is unvisited
                if (match_[u] == -1) {
                    // Augmenting path found — trace back and flip
                    pred[u] = v;
                    int cur = u;
                    while (cur != -1) {
                        int pv = pred[cur];
                        int ppv = match_[pv];
                        match_[cur] = pv;
                        match_[pv] = cur;
                        cur = ppv;
                    }
                    return true;
                }
                // Extend tree: u is INNER, match_[u] is OUTER
                label[u] = -1;
                pred[u] = v;
                int w = match_[u];
                label[w] = 1;
                pred[w] = u;   // track predecessor so markPath can traverse up
                q.push(w);
            } else {
                // u is OUTER — potential blossom
                int b = lca(v, u);
                markPath(v, b, u);
                markPath(u, b, v);
            }
        }
    }
    return false;
}

int Blossom::maxMatching() {
    match_.assign(n_, -1);
    int result = 0;
    for (int v = 0; v < n_; ++v) {
        if (match_[v] == -1 && augment(v))
            ++result;
    }
    return result;
}
