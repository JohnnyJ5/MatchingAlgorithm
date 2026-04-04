// Unit tests for DBManager.
//
// All tests use an invalid connection string so every internal ConnPool
// connection attempt fails immediately.  This exercises two guarantees:
//
//  1. The constructor must not throw — it logs a warning and continues with
//     an empty pool (partial-degradation contract from the header comment).
//
//  2. Every public method that must touch the database acquires a connection
//     first.  With an empty pool that acquisition throws std::runtime_error.
//     The methods must *not* silently swallow the exception; they must let it
//     propagate so the caller knows the operation failed.  A method that
//     catches and discards the pool exception would return a bogus
//     default-constructed value and hide the failure.
//
// These tests also verify that no method performs a hard crash or throws
// anything other than std::runtime_error when the pool is exhausted.

#include "../db_manager.h"

#include <iostream>
#include <stdexcept>
#include <string>

static int failures = 0;

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (!cond) ++failures;
}

static constexpr const char* kBadConnStr =
    "host=127.0.0.1 port=9 dbname=nonexistent connect_timeout=1";

// Returns a DBManager backed by an empty pool (no live connections).
// Uses poolSize=1 to keep connection-attempt time short (one failure only).
static DBManager makeDeadManager() {
    return DBManager(kBadConnStr, 1);
}

// Helper: calls fn(), returns true iff it throws std::runtime_error.
// Lambdas that return [[nodiscard]] values should use `return expr` so
// the (void) cast here suppresses the unused-result warning at the call site.
template<typename Fn>
static bool throwsRuntimeError(Fn fn) {
    try {
        (void)fn();
        return false;
    } catch (const std::runtime_error&) {
        return true;
    } catch (...) {
        return false;
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────

// The constructor must not throw even when every connection fails.
// The header documents "partial degradation" — the server should start and
// serve non-DB endpoints while logging the connection failure.
static void test_constructor_does_not_throw_on_bad_connstr() {
    bool ok = true;
    try {
        DBManager mgr(kBadConnStr, 2);
    } catch (...) {
        ok = false;
    }
    check("ctor: does not throw with bad connstr", ok);
}

// ── User management ───────────────────────────────────────────────────────────

static void test_get_user_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("getUser: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.getUser(1); }));
}

static void test_login_user_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("loginUser: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.loginUser("a@b.com", "pass"); }));
}

static void test_list_users_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("listUsers: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.listUsers(1, 10); }));
}

static void test_list_users_with_filter_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("listUsers(filtered): throws runtime_error",
          throwsRuntimeError([&]{ return mgr.listUsers(1, 10, "female"); }));
}

static void test_register_user_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("registerUser: throws runtime_error",
          throwsRuntimeError([&]{
              return mgr.registerUser("alice", "Alice Smith", "a@b.com",
                                     "secret", "female", 25, "bio", {});
          }));
}

static void test_update_user_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    UpdateFields f;
    f.alias = "newname";
    check("updateUser: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.updateUser(1, f); }));
}

static void test_delete_user_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("deleteUser: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.deleteUser(1); }));
}

// ── Questionnaire ─────────────────────────────────────────────────────────────

static void test_submit_questionnaire_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("submitQuestionnaire: throws runtime_error",
          throwsRuntimeError([&]{
              return mgr.submitQuestionnaire(1, 1, {{1, "yes"}, {2, "7"}});
          }));
}

// ── Compatibility ─────────────────────────────────────────────────────────────

static void test_get_compat_matrix_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("getCompatibilityMatrix: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.getCompatibilityMatrix(); }));
}

static void test_get_compat_matrix_threshold_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("getCompatibilityMatrixAboveThreshold: throws runtime_error",
          throwsRuntimeError([&]{
              return mgr.getCompatibilityMatrixAboveThreshold(70);
          }));
}

// ── Events ────────────────────────────────────────────────────────────────────

static void test_list_events_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("listEvents: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.listEvents(); }));
}

static void test_get_event_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("getEvent: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.getEvent(1); }));
}

static void test_register_for_event_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("registerForEvent: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.registerForEvent(1, 42); }));
}

// ── Matches ───────────────────────────────────────────────────────────────────

static void test_list_matches_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("listMatchesForUser: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.listMatchesForUser(1); }));
}

static void test_accept_match_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("acceptMatch: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.acceptMatch(1, 42); }));
}

static void test_decline_match_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("declineMatch: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.declineMatch(1, 42); }));
}

// ── Messages ──────────────────────────────────────────────────────────────────

static void test_get_messages_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("getMessages: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.getMessages(1, 50); }));
}

static void test_send_message_propagates_pool_exception() {
    auto mgr = makeDeadManager();
    check("sendMessage: throws runtime_error",
          throwsRuntimeError([&]{ return mgr.sendMessage(1, 42, "hello"); }));
}

int main() {
    test_constructor_does_not_throw_on_bad_connstr();

    test_get_user_propagates_pool_exception();
    test_login_user_propagates_pool_exception();
    test_list_users_propagates_pool_exception();
    test_list_users_with_filter_propagates_pool_exception();
    test_register_user_propagates_pool_exception();
    test_update_user_propagates_pool_exception();
    test_delete_user_propagates_pool_exception();

    test_submit_questionnaire_propagates_pool_exception();

    test_get_compat_matrix_propagates_pool_exception();
    test_get_compat_matrix_threshold_propagates_pool_exception();

    test_list_events_propagates_pool_exception();
    test_get_event_propagates_pool_exception();
    test_register_for_event_propagates_pool_exception();

    test_list_matches_propagates_pool_exception();
    test_accept_match_propagates_pool_exception();
    test_decline_match_propagates_pool_exception();

    test_get_messages_propagates_pool_exception();
    test_send_message_propagates_pool_exception();

    if (failures > 0)
        std::cout << failures << " DBManager test(s) FAILED.\n";
    else
        std::cout << "All DBManager tests passed.\n";
    return failures > 0 ? 1 : 0;
}
