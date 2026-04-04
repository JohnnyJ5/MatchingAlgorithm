#include "conn_pool.h"

#include <iostream>
#include <stdexcept>

// ── ConnGuard ────────────────────────────────────────────────────────────────

ConnGuard::ConnGuard(ConnPool& pool, PGconn* conn) noexcept
    : pool_(&pool), conn_(conn) {}

ConnGuard::~ConnGuard() {
    if (pool_)
        pool_->release(conn_);
}

ConnGuard::ConnGuard(ConnGuard&& other) noexcept
    : pool_(other.pool_), conn_(other.conn_) {
    other.pool_ = nullptr;
    other.conn_ = nullptr;
}

bool ConnGuard::ok() const noexcept {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

// ── ConnPool ─────────────────────────────────────────────────────────────────

ConnPool::ConnPool(std::string_view connstr, std::size_t size) {
    all_.reserve(size);
    available_.reserve(size);

    const std::string cs(connstr);
    for (std::size_t i = 0; i < size; ++i) {
        PGconn* conn = PQconnectdb(cs.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            std::cerr << "[ConnPool] connection " << i
                      << " failed: " << PQerrorMessage(conn) << "\n";
            PQfinish(conn);
            continue;
        }
        std::cout << "[ConnPool] connection " << i << " OK\n";
        all_.push_back(conn);
        available_.push_back(conn);
    }

    if (all_.empty()) {
        std::cerr << "[ConnPool] WARNING — no connections available; "
                     "all database endpoints will return errors.\n";
    }
}

ConnPool::~ConnPool() {
    for (auto* conn : all_)
        PQfinish(conn);
}

ConnGuard ConnPool::acquire() {
    return acquireWithTimeout(kDefaultTimeout);
}

ConnGuard ConnPool::acquireWithTimeout(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const bool got = cv_.wait_for(lock, timeout,
                                  [this]{ return !available_.empty(); });
    if (!got)
        throw std::runtime_error("ConnPool: connection pool exhausted (timeout)");

    PGconn* conn = available_.back();
    available_.pop_back();
    lock.unlock();

    // Attempt reconnect if the connection was dropped while idle.
    if (PQstatus(conn) != CONNECTION_OK) {
        PQreset(conn);
        if (PQstatus(conn) != CONNECTION_OK) {
            // Return to pool so pool size stays correct, then surface the error.
            release(conn);
            throw std::runtime_error("ConnPool: connection reset failed");
        }
    }
    return ConnGuard(*this, conn);
}

void ConnPool::release(PGconn* conn) noexcept {
    {
        std::lock_guard lock(mutex_);
        available_.push_back(conn);
    }
    cv_.notify_one();
}

std::size_t ConnPool::size() const noexcept {
    std::lock_guard lock(mutex_);
    return all_.size();
}

std::size_t ConnPool::available() const noexcept {
    std::lock_guard lock(mutex_);
    return available_.size();
}
