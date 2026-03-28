#pragma once
#include <vector>

// Hungarian algorithm for maximum-weight bipartite assignment.
//
// Solves the square assignment problem on an n×n cost matrix.
// The implementation maximises total score (negates internally for min-cost).
//
// Time: O(n³)   Space: O(n²)
class Hungarian {
public:
    // costMatrix[i][j] = compatibility score between left-person i and right-person j.
    explicit Hungarian(const std::vector<std::vector<int>>& costMatrix);

    // Solve the assignment problem.
    void solve();

    // After solve(): total maximum score.
    int getMaxScore() const { return maxScore_; }

    // After solve(): assignment[i] = j means left-person i is assigned to right-person j.
    const std::vector<int>& getAssignment() const { return assignment_; }

private:
    int n_;
    std::vector<std::vector<int>> cost_;  // stored as negated for min-cost

    int maxScore_;
    std::vector<int> assignment_;
};
