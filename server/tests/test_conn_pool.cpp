// Unit tests for ConnPool and ConnGuard.
//
// These tests use an invalid connection string (port 9, which is reserved and
// will refuse immediately) so all PQconnectdb calls fail and the pool contains
// zero live connections.  This lets us test pool accounting, exception
// behaviour, and timeout precision without a running PostgreSQL server.

#include "../conn_pool.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

static int failures = 0;

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (!cond) ++failures;
}

// Connects to port 9 (discard; always refused) on localhost.
// connect_timeout=1 keeps PQconnectdb from hanging more than 1 s per attempt.
static constexpr const char* kBadConnStr =
    "host=127.0.0.1 port=9 dbname=nonexistent connect_timeout=1";

// ── Pool construction with all-failing connections ────────────────────────────

// When every connection attempt fails, the pool must report zero live
// connections.  A bug in the construction loop could leave stale pointers in
// all_ / available_ and make size() or available() return garbage.
static void test_bad_connstr_reports_zero_size() {
    ConnPool pool(kBadConnStr, 4);
    check("bad-connstr: size() == 0",      pool.size() == 0);
    check("bad-connstr: available() == 0", pool.available() == 0);
}

// Requesting exactly zero connections is a valid edge case and must not crash.
static void test_zero_requested_size() {
    ConnPool pool(kBadConnStr, 0);
    check("size-0: size() == 0",      pool.size() == 0);
    check("size-0: available() == 0", pool.available() == 0);
}

// ── acquire() on an empty pool ────────────────────────────────────────────────

// acquire() (default timeout) must throw std::runtime_error, not block forever
// or throw a different exception type.
static void test_acquire_on_empty_pool_throws_runtime_error() {
    ConnPool pool(kBadConnStr, 2);
    bool threw_correct_type = false;
    try {
        // Use a short timeout so the test doesn't wait the full 5 s default.
        [[maybe_unused]] auto _cg = pool.acquire(std::chrono::milliseconds(100));
    } catch (const std::runtime_error&) {
        threw_correct_type = true;
    } catch (...) {
        // Wrong exception type — that is a bug.
    }
    check("acquire-empty: throws std::runtime_error", threw_correct_type);
}

// The exception message must mention exhaustion or timeout so callers can
// diagnose the problem.  An empty or misleading message is a usability bug.
static void test_acquire_exception_message_is_informative() {
    ConnPool pool(kBadConnStr, 1);
    try {
        [[maybe_unused]] auto _cg = pool.acquire(std::chrono::milliseconds(50));
        check("acquire-message: exception was thrown", false);
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        const bool informative =
            msg.find("pool")      != std::string::npos ||
            msg.find("Pool")      != std::string::npos ||
            msg.find("exhausted") != std::string::npos ||
            msg.find("timeout")   != std::string::npos;
        check("acquire-message: message mentions pool/exhausted/timeout", informative);
    }
}

// ── Timeout precision ─────────────────────────────────────────────────────────

// acquire(100 ms) must return within a generous 3-second wall-clock budget.
// If the pool ignores the supplied timeout and blocks for the default 5 s,
// this test catches it.
static void test_short_timeout_respected() {
    ConnPool pool(kBadConnStr, 1);
    const auto t0 = std::chrono::steady_clock::now();
    try {
        [[maybe_unused]] auto _cg = pool.acquire(std::chrono::milliseconds(100));
    } catch (const std::runtime_error&) {}
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    check("short-timeout: completes in < 3 s",
          elapsed < std::chrono::seconds(3));
}

// A longer timeout must not expire faster than requested.
// acquire(200 ms) should take AT LEAST 150 ms (20 % slack for scheduler jitter).
static void test_timeout_not_too_early() {
    ConnPool pool(kBadConnStr, 1);
    const auto t0 = std::chrono::steady_clock::now();
    try {
        [[maybe_unused]] auto _cg = pool.acquire(std::chrono::milliseconds(200));
    } catch (const std::runtime_error&) {}
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    check("timeout-not-too-early: waited at least 150 ms",
          elapsed >= std::chrono::milliseconds(150));
}

// ── Destructor safety ─────────────────────────────────────────────────────────

// Constructing and immediately destroying a pool that opened no connections
// must not crash or throw (PQfinish is a no-op for null / invalid pointers
// only if the pool correctly skips them; a bug could call PQfinish on garbage).
static void test_destructor_with_zero_connections_is_safe() {
    bool ok = true;
    try {
        ConnPool pool(kBadConnStr, 3);
        // pool destructs here — must not crash
    } catch (...) {
        ok = false;
    }
    check("destructor: empty pool destructs without exception", ok);
}

// ── release() bookkeeping ─────────────────────────────────────────────────────

// release() is the mechanism that returns connections to the pool and
// unblocks waiting threads.  We can verify the accounting path by calling
// release(nullptr) — which pushes nullptr onto available_.  This confirms the
// mutex + notify path works, and that available() increments correctly even
// with a null pointer (representing a placeholder in this test).
//
// Note: after this test the pool holds a null pointer in available_; the pool
// object goes out of scope immediately, so no acquire() can race against it.
static void test_release_increments_available() {
    ConnPool pool(kBadConnStr, 0);  // starts at 0
    check("release: available before == 0", pool.available() == 0);
    pool.release(nullptr);  // simulate returning a connection
    check("release: available after == 1",  pool.available() == 1);
}

// ── ConnGuard move semantics ──────────────────────────────────────────────────

// After a ConnGuard is move-constructed, the *source* guard must not call
// release() when it destructs (its pool_ pointer is nulled out by the move).
// We verify this by observing that available() does not double-increment.
static void test_connguard_move_does_not_double_release() {
    ConnPool pool(kBadConnStr, 0);
    pool.release(nullptr);           // seed one "connection" so available==1
    check("guard-move: pre-condition available==1", pool.available() == 1);

    {
        // Manually construct a guard wrapping the null "connection".
        // acquire() would block (it checks CONNECTION_OK), so we construct
        // directly using the public constructor.
        ConnGuard g1(pool, nullptr);
        // g1 now holds the connection; pool sees 0 available (we did not pop
        // via acquire, so available is still 1 — we are simulating the state
        // after a real acquire would have popped it).
        // We just care that after move, only ONE release() fires.
        ConnGuard g2(std::move(g1));  // g1.pool_ == nullptr after this
        // g2 destructs → one release() call → available += 1
        // g1 destructs → no release() call   → available unchanged
    }
    // Two destructor calls, but only one release().
    // available was 1 before constructing g1 (we didn't pop it), and each
    // release() adds 1.  After one release() from g2: available == 2.
    // If g1 also fires: available == 3.  That would be the bug.
    check("guard-move: exactly one release fired (available == 2)",
          pool.available() == 2);
}

int main() {
    test_bad_connstr_reports_zero_size();
    test_zero_requested_size();
    test_acquire_on_empty_pool_throws_runtime_error();
    test_acquire_exception_message_is_informative();
    test_short_timeout_respected();
    test_timeout_not_too_early();
    test_destructor_with_zero_connections_is_safe();
    test_release_increments_available();
    test_connguard_move_does_not_double_release();
    if (failures > 0)
        std::cout << failures << " ConnPool test(s) FAILED.\n";
    else
        std::cout << "All ConnPool tests passed.\n";
    return failures > 0 ? 1 : 0;
}
