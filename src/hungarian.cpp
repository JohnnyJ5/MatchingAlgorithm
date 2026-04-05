#include "hungarian.h"
#include <cassert>
#include <algorithm>
#include <limits>

Hungarian::Hungarian(const std::vector<std::vector<int>>& costMatrix)
    : n_(static_cast<int>(costMatrix.size())), maxScore_(0)
{
    assert(!costMatrix.empty());
    // Convert to minimisation by negating.
    cost_.resize(n_, std::vector<int>(n_));
    for (int i = 0; i < n_; ++i) {
        assert(static_cast<int>(costMatrix[i].size()) == n_);
        for (int j = 0; j < n_; ++j)
            cost_[i][j] = -costMatrix[i][j];
    }
}

// Classic O(n³) Hungarian using the Jonker-Volgenant / Kuhn reduction approach
// with potential (dual) variables.
void Hungarian::solve() {
    const int INF = std::numeric_limits<int>::max() / 2;

    // u[i] = potential for row i (0..n-1), v[j] = potential for col j (0..n-1).
    // row 0 is a dummy row, so arrays are size n+1.
    std::vector<int> u(n_ + 1, 0);
    std::vector<int> v(n_ + 1, 0);
    std::vector<int> p(n_ + 1, 0);   // p[j] = row assigned to column j (0 = unassigned)
    std::vector<int> way(n_ + 1, 0); // augmentation path

    for (int i = 1; i <= n_; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<int> minv(n_ + 1, INF);
        std::vector<bool> used(n_ + 1, false);

        do {
            used[j0] = true;
            int i0 = p[j0];
            int delta = INF;
            int j1 = -1;
            for (int j = 1; j <= n_; ++j) {
                if (!used[j]) {
                    int cur = cost_[i0 - 1][j - 1] - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1 = j;
                    }
                }
            }
            for (int j = 0; j <= n_; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j]    -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    assignment_.assign(n_, -1);
    for (int j = 1; j <= n_; ++j)
        if (p[j] != 0)
            assignment_[p[j] - 1] = j - 1;

    maxScore_ = 0;
    for (int i = 0; i < n_; ++i)
        maxScore_ += -cost_[i][assignment_[i]];
}
