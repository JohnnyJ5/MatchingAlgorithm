// ============================================================================
// ConnPool / ConnGuard — thread-safe PostgreSQL connection pool.
//
// ConnPool opens a fixed number of libpq connections at construction and keeps
// them alive for the lifetime of the pool.  Each call to acquire() hands out
// one connection wrapped in a ConnGuard; the connection is returned
// automatically when the guard goes out of scope.
//
// Concurrency contract:
//   • acquire() blocks until a connection is available (or the timeout elapses).
//   • ~ConnGuard calls release(), which is safe to call from any thread.
//   • The pool itself is not copyable or movable.
//
// Error handling:
//   If a connection is found to be unhealthy on acquire(), the pool attempts
//   PQreset() before returning it.  If the reset fails the connection is
//   returned to the available set and std::runtime_error is thrown so the
//   caller gets a timely error rather than a silent bad connection.
//   If the timeout expires before any connection is available,
//   std::runtime_error is thrown.
// ============================================================================
#pragma once

#include <libpq-fe.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

class ConnPool;

// ── ConnGuard ────────────────────────────────────────────────────────────────

// RAII wrapper around a PGconn* checked out from a ConnPool.
// Move-only.  On destruction the connection is returned to the pool.
class ConnGuard {
public:
    ConnGuard(ConnPool& pool, PGconn* conn) noexcept;
    ~ConnGuard();

    ConnGuard(ConnGuard&& other) noexcept;
    ConnGuard& operator=(ConnGuard&&) = delete;
    ConnGuard(const ConnGuard&)       = delete;
    ConnGuard& operator=(const ConnGuard&) = delete;

    // Raw pointer access.
    [[nodiscard]] PGconn* get()        const noexcept { return conn_; }
    [[nodiscard]] PGconn* operator->() const noexcept { return conn_; }

    // True iff the connection is non-null and in a healthy state.
    [[nodiscard]] bool ok() const noexcept;

private:
    ConnPool* pool_;  // null after move
    PGconn*   conn_;  // null after move
};

// ── ConnPool ─────────────────────────────────────────────────────────────────

// Thread-safe pool of libpq PGconn* connections.
class ConnPool {
public:
    // Default timeout used by acquire() when no explicit timeout is given.
    static constexpr std::chrono::seconds kDefaultTimeout{5};

    // Opens `size` connections using `connstr` (standard libpq connection
    // string).  Failed connections are discarded; the pool is smaller if some
    // fail.  If zero connections succeed, a warning is printed and every
    // subsequent acquire() will immediately time out.
    ConnPool(std::string_view connstr, std::size_t size);
    ~ConnPool();

    ConnPool(const ConnPool&)            = delete;
    ConnPool& operator=(const ConnPool&) = delete;

    // Block until a connection is available, then return a ConnGuard.
    // Uses kDefaultTimeout.  Throws std::runtime_error on timeout or if the
    // acquired connection cannot be reset after a dropped connection.
    [[nodiscard]] ConnGuard acquire();

    // Same as acquire() but with an explicit timeout duration.
    template<typename Rep, typename Period>
    [[nodiscard]] ConnGuard acquire(std::chrono::duration<Rep, Period> timeout);

    // Returns a connection to the available set.  Called by ~ConnGuard.
    // noexcept because it must not throw from a destructor.
    void release(PGconn* conn) noexcept;

    // Total number of live connections owned by the pool.
    [[nodiscard]] std::size_t size() const noexcept;

    // Number of connections currently available (not checked out).
    [[nodiscard]] std::size_t available() const noexcept;

private:
    // Implementation shared by both acquire() overloads.
    [[nodiscard]] ConnGuard acquireWithTimeout(std::chrono::milliseconds timeout);

    std::vector<PGconn*>    all_;        // owns every live connection
    std::vector<PGconn*>    available_;  // connections ready to be acquired
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
};

// ── Template implementation ───────────────────────────────────────────────────

template<typename Rep, typename Period>
ConnGuard ConnPool::acquire(std::chrono::duration<Rep, Period> timeout) {
    return acquireWithTimeout(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
}
