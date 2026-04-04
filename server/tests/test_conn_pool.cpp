// GTest suite for ConnPool and ConnGuard.
//
// Uses an invalid connection string (port 9, always refused) so all
// PQconnectdb calls fail — no running PostgreSQL server required.
// Tests cover: pool accounting on construction failure, acquire() exception
// type and message, timeout precision, destructor safety, release()
// bookkeeping, and ConnGuard move-semantics.

#include "../conn_pool.h"

#include <gtest/gtest.h>
#include <chrono>
#include <stdexcept>
#include <string>

// connect_timeout=1 keeps each PQconnectdb attempt from hanging more than 1 s.
static constexpr const char* kBadConnStr =
    "host=127.0.0.1 port=9 dbname=nonexistent connect_timeout=1";

// ── Pool construction with all-failing connections ────────────────────────────

TEST(ConnPoolTest, BadConnstrReportsZeroSize) {
    ConnPool pool(kBadConnStr, 4);
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_EQ(pool.available(), 0u);
}

TEST(ConnPoolTest, ZeroRequestedSize) {
    ConnPool pool(kBadConnStr, 0);
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_EQ(pool.available(), 0u);
}

// ── acquire() on an empty pool ────────────────────────────────────────────────

TEST(ConnPoolTest, AcquireOnEmptyPoolThrowsRuntimeError) {
    ConnPool pool(kBadConnStr, 2);
    EXPECT_THROW(
        { [[maybe_unused]] auto cg = pool.acquire(std::chrono::milliseconds(100)); },
        std::runtime_error);
}

// The exception message must mention exhaustion/timeout — not be empty or cryptic.
TEST(ConnPoolTest, AcquireExceptionMessageIsInformative) {
    ConnPool pool(kBadConnStr, 1);
    try {
        [[maybe_unused]] auto cg = pool.acquire(std::chrono::milliseconds(50));
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        const bool informative =
            msg.find("pool")      != std::string::npos ||
            msg.find("Pool")      != std::string::npos ||
            msg.find("exhausted") != std::string::npos ||
            msg.find("timeout")   != std::string::npos;
        EXPECT_TRUE(informative) << "Uninformative exception message: " << msg;
    }
}

// ── Timeout precision ─────────────────────────────────────────────────────────

// acquire(100 ms) must not block for the default 5-second timeout.
TEST(ConnPoolTest, ShortTimeoutRespectsDeadline) {
    ConnPool pool(kBadConnStr, 1);
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_THROW(
        { [[maybe_unused]] auto cg = pool.acquire(std::chrono::milliseconds(100)); },
        std::runtime_error);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(elapsed, std::chrono::seconds(3))
        << "Short timeout took too long — may have used the 5-second default";
}

// acquire(200 ms) must not expire before ~150 ms (20 % lower slack).
TEST(ConnPoolTest, TimeoutNotTooEarly) {
    ConnPool pool(kBadConnStr, 1);
    const auto t0 = std::chrono::steady_clock::now();
    try {
        [[maybe_unused]] auto cg = pool.acquire(std::chrono::milliseconds(200));
    } catch (const std::runtime_error&) {}
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_GE(elapsed, std::chrono::milliseconds(150))
        << "Timeout fired too early";
}

// ── Destructor safety ─────────────────────────────────────────────────────────

// Constructing and immediately destroying a 0-connection pool must not crash.
TEST(ConnPoolTest, DestructorWithZeroConnectionsIsSafe) {
    EXPECT_NO_THROW({
        ConnPool pool(kBadConnStr, 3);
    });
}

// ── release() bookkeeping ─────────────────────────────────────────────────────

// release() must increment available() so the condition variable can unblock
// a waiting acquire().  We call release(nullptr) as a lightweight probe.
TEST(ConnPoolTest, ReleaseIncrementsAvailable) {
    ConnPool pool(kBadConnStr, 0);
    EXPECT_EQ(pool.available(), 0u);
    pool.release(nullptr);
    EXPECT_EQ(pool.available(), 1u);
}

// ── ConnGuard move semantics ──────────────────────────────────────────────────

// After a move-construction, the *source* guard's destructor must not call
// release() a second time (pool_ is nulled out by the move).  We verify by
// counting how many times available() increments.
TEST(ConnPoolTest, ConnGuardMoveDoesNotDoubleRelease) {
    ConnPool pool(kBadConnStr, 0);
    pool.release(nullptr);  // seed one "connection"
    ASSERT_EQ(pool.available(), 1u);

    {
        ConnGuard g1(pool, nullptr);
        ConnGuard g2(std::move(g1));  // g1.pool_ becomes nullptr
        // g2 destructs → one release()  → available += 1
        // g1 destructs → no release()   → available unchanged
    }
    // Exactly one release() should have fired → available == 2.
    EXPECT_EQ(pool.available(), 2u)
        << "More than one release() fired — ConnGuard move semantics broken";
}
