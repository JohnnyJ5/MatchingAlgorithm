// GTest suite for DBManager.
//
// All tests use an invalid connection string so every ConnPool connection
// attempt fails immediately.  This exercises two guarantees:
//
//  1. Constructor must not throw — partial-degradation contract.
//  2. Every public method propagates std::runtime_error from pool exhaustion
//     rather than swallowing it silently.

#include "../db_manager.h"

#include <gtest/gtest.h>
#include <stdexcept>

static constexpr const char* kBadConnStr =
    "host=127.0.0.1 port=9 dbname=nonexistent connect_timeout=1";

// DBManager backed by an empty pool.  poolSize=1 keeps connection-attempt
// time short (one failure per test).
static DBManager makeDeadManager() {
    return DBManager(kBadConnStr, 1);
}

// ── Constructor ───────────────────────────────────────────────────────────────

TEST(DBManagerTest, ConstructorDoesNotThrowOnBadConnstr) {
    EXPECT_NO_THROW({ DBManager mgr(kBadConnStr, 2); });
}

// ── User management ───────────────────────────────────────────────────────────

TEST(DBManagerTest, GetUserPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.getUser(1); }, std::runtime_error);
}

TEST(DBManagerTest, LoginUserPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.loginUser("a@b.com", "pass"); }, std::runtime_error);
}

TEST(DBManagerTest, ListUsersPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.listUsers(1, 10); }, std::runtime_error);
}

TEST(DBManagerTest, ListUsersWithFilterPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.listUsers(1, 10, "female"); }, std::runtime_error);
}

TEST(DBManagerTest, RegisterUserPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW(
        {
            (void)mgr.registerUser("alice", "Alice Smith", "a@b.com",
                                   "secret", "female", 25, "bio", {});
        },
        std::runtime_error);
}

TEST(DBManagerTest, UpdateUserPropagatesPoolException) {
    auto mgr = makeDeadManager();
    UpdateFields f;
    f.alias = "newname";
    EXPECT_THROW({ (void)mgr.updateUser(1, f); }, std::runtime_error);
}

TEST(DBManagerTest, DeleteUserPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.deleteUser(1); }, std::runtime_error);
}

// ── Questionnaire ─────────────────────────────────────────────────────────────

TEST(DBManagerTest, SubmitQuestionnairePropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW(
        { (void)mgr.submitQuestionnaire(1, 1, {{1, "yes"}, {2, "7"}}); },
        std::runtime_error);
}

// ── Compatibility ─────────────────────────────────────────────────────────────

TEST(DBManagerTest, GetCompatibilityMatrixPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.getCompatibilityMatrix(); }, std::runtime_error);
}

TEST(DBManagerTest, GetCompatibilityMatrixThresholdPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW(
        { (void)mgr.getCompatibilityMatrixAboveThreshold(70); },
        std::runtime_error);
}

// ── Events ────────────────────────────────────────────────────────────────────

TEST(DBManagerTest, ListEventsPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.listEvents(); }, std::runtime_error);
}

TEST(DBManagerTest, GetEventPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.getEvent(1); }, std::runtime_error);
}

TEST(DBManagerTest, RegisterForEventPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.registerForEvent(1, 42); }, std::runtime_error);
}

// ── Matches ───────────────────────────────────────────────────────────────────

TEST(DBManagerTest, ListMatchesPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.listMatchesForUser(1); }, std::runtime_error);
}

TEST(DBManagerTest, AcceptMatchPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.acceptMatch(1, 42); }, std::runtime_error);
}

TEST(DBManagerTest, DeclineMatchPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.declineMatch(1, 42); }, std::runtime_error);
}

// ── Messages ──────────────────────────────────────────────────────────────────

TEST(DBManagerTest, GetMessagesPropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.getMessages(1, 50); }, std::runtime_error);
}

TEST(DBManagerTest, SendMessagePropagatesPoolException) {
    auto mgr = makeDeadManager();
    EXPECT_THROW({ (void)mgr.sendMessage(1, 42, "hello"); }, std::runtime_error);
}
