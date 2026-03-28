#include "hopcroft_karp.h"
#include <queue>
#include <cassert>

HopcroftKarp::HopcroftKarp(int leftSize, int rightSize)
    : left_(leftSize), right_(rightSize),
      adj_(leftSize),
      matchLeft_(leftSize, -1),
      matchRight_(rightSize, -1),
      distLeft_(leftSize)
{}

void HopcroftKarp::addCompatiblePair(int a, int b) {
    assert(a >= 0 && a < left_);
    assert(b >= 0 && b < right_);
    adj_[a].push_back(b);
}

bool HopcroftKarp::bfs() {
    std::queue<int> q;
    for (int a = 0; a < left_; ++a) {
        if (matchLeft_[a] == -1) {
            distLeft_[a] = 0;
            q.push(a);
        } else {
            distLeft_[a] = INF;
        }
    }
    bool found = false;
    while (!q.empty()) {
        int a = q.front(); q.pop();
        for (int b : adj_[a]) {
            int a2 = matchRight_[b];
            if (a2 == -1) {
                found = true;
            } else if (distLeft_[a2] == INF) {
                distLeft_[a2] = distLeft_[a] + 1;
                q.push(a2);
            }
        }
    }
    return found;
}

bool HopcroftKarp::dfs(int a) {
    for (int b : adj_[a]) {
        int a2 = matchRight_[b];
        if (a2 == -1 || (distLeft_[a2] == distLeft_[a] + 1 && dfs(a2))) {
            matchLeft_[a]  = b;
            matchRight_[b] = a;
            return true;
        }
    }
    distLeft_[a] = INF;
    return false;
}

int HopcroftKarp::maxMatching() {
    matchLeft_.assign(left_,  -1);
    matchRight_.assign(right_, -1);

    int matching = 0;
    while (bfs()) {
        for (int a = 0; a < left_; ++a)
            if (matchLeft_[a] == -1 && dfs(a))
                ++matching;
    }
    return matching;
}
