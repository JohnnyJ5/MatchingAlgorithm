// Unit tests for db_types.h — DbResult<T>, dbOk/dbValue/dbError helpers, DbErrc.
//
// No libpq dependency.  These types are the return contract for every
// DBManager method; bugs here silently corrupt all DB-layer error handling.

#include "../db_types.h"

#include <iostream>
#include <string>
#include <variant>

static int failures = 0;

static void check(const char* name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (!cond) ++failures;
}

// ── dbOk / dbValue ────────────────────────────────────────────────────────────

// A result constructed from a plain int must be "ok".
static void test_value_is_ok() {
    DbResult<int> r = 42;
    check("value-is-ok: dbOk true",       dbOk(r));
    check("value-is-ok: dbValue == 42",   dbValue(r) == 42);
}

// A result constructed from DbError must not be "ok".
static void test_error_is_not_ok() {
    DbResult<int> r = DbError{DbErrc::NotFound, "user not found"};
    check("error-not-ok: dbOk false",          !dbOk(r));
    check("error-not-ok: code == NotFound",    dbError(r).code == DbErrc::NotFound);
    check("error-not-ok: message preserved",   dbError(r).message == "user not found");
}

// ── DbResult<std::monostate> ──────────────────────────────────────────────────

// updateUser() and deleteUser() return DbResult<std::monostate> on success.
// The void-like success case must correctly report ok.
static void test_monostate_ok() {
    DbResult<std::monostate> r = std::monostate{};
    check("monostate-ok: dbOk true", dbOk(r));
}

// Error variant of monostate result must report not-ok with correct code.
static void test_monostate_error() {
    DbResult<std::monostate> r = DbError{DbErrc::InvalidInput, "no fields"};
    check("monostate-err: dbOk false",   !dbOk(r));
    check("monostate-err: code",          dbError(r).code == DbErrc::InvalidInput);
    check("monostate-err: message",       dbError(r).message == "no fields");
}

// ── Complex struct round-trip ─────────────────────────────────────────────────

// DbResult<RegisteredUser> must preserve every field of the stored struct.
static void test_registered_user_roundtrip() {
    RegisteredUser u{99, "bob"};
    DbResult<RegisteredUser> r = u;
    check("reg-user: ok",     dbOk(r));
    check("reg-user: id",     dbValue(r).id == 99);
    check("reg-user: alias",  dbValue(r).alias == "bob");
}

// DbResult<LoginResult> — a wider struct with three fields.
static void test_login_result_roundtrip() {
    LoginResult lr{7, "carol", "admin"};
    DbResult<LoginResult> r = lr;
    check("login-result: ok",     dbOk(r));
    check("login-result: userId", dbValue(r).userId == 7);
    check("login-result: alias",  dbValue(r).alias == "carol");
    check("login-result: role",   dbValue(r).role == "admin");
}

// ── Reassignment ──────────────────────────────────────────────────────────────

// Re-assigning a previously-ok result to an error must flip the state.
// Guards against a result type that caches the original discriminant.
static void test_reassign_value_to_error() {
    DbResult<int> r = 100;
    check("reassign: initially ok",   dbOk(r));
    r = DbError{DbErrc::Conflict, "duplicate"};
    check("reassign: now error",      !dbOk(r));
    check("reassign: new code",       dbError(r).code == DbErrc::Conflict);
}

// Re-assigning a previously-error result to a value must flip back.
static void test_reassign_error_to_value() {
    DbResult<std::string> r = DbError{DbErrc::InternalError, "oops"};
    check("reassign-back: initially error", !dbOk(r));
    r = std::string("recovered");
    check("reassign-back: now ok",          dbOk(r));
    check("reassign-back: value",           dbValue(r) == "recovered");
}

// ── DbErrc distinctness ───────────────────────────────────────────────────────

// All DbErrc enum values must be pairwise distinct.
// An accidental duplicate would make error-code comparisons unreliable.
static void test_dberrc_values_all_distinct() {
    DbErrc vals[] = {
        DbErrc::Ok,
        DbErrc::NotFound,
        DbErrc::Conflict,
        DbErrc::Unauthorized,
        DbErrc::InvalidInput,
        DbErrc::InternalError,
    };
    const int n = static_cast<int>(sizeof(vals) / sizeof(vals[0]));
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            check("dberrc-distinct: no duplicate enum values", vals[i] != vals[j]);
}

// ── DbError struct ────────────────────────────────────────────────────────────

// DbError must independently store code and message; one must not alias the other.
static void test_dberror_fields_independent() {
    DbError e{DbErrc::Unauthorized, "bad password"};
    check("dberror-fields: code",    e.code == DbErrc::Unauthorized);
    check("dberror-fields: message", e.message == "bad password");
    // Mutate message and verify code is unchanged.
    e.message = "changed";
    check("dberror-fields: code unchanged after message mutation",
          e.code == DbErrc::Unauthorized);
}

// ── Vector result ─────────────────────────────────────────────────────────────

// DbResult<vector<CompatibilityScore>> is used by getCompatibilityMatrix().
// Verify the template instantiates and preserves element count and values.
static void test_vector_result_roundtrip() {
    std::vector<CompatibilityScore> scores = {{1, 2, 85}, {1, 3, 72}};
    DbResult<std::vector<CompatibilityScore>> r = scores;
    check("vec-result: ok",           dbOk(r));
    check("vec-result: size 2",       dbValue(r).size() == 2);
    check("vec-result: first score",  dbValue(r)[0].score == 85);
    check("vec-result: second manId", dbValue(r)[1].manId == 1);
}

int main() {
    test_value_is_ok();
    test_error_is_not_ok();
    test_monostate_ok();
    test_monostate_error();
    test_registered_user_roundtrip();
    test_login_result_roundtrip();
    test_reassign_value_to_error();
    test_reassign_error_to_value();
    test_dberrc_values_all_distinct();
    test_dberror_fields_independent();
    test_vector_result_roundtrip();
    if (failures > 0)
        std::cout << failures << " DbTypes test(s) FAILED.\n";
    else
        std::cout << "All DbTypes tests passed.\n";
    return failures > 0 ? 1 : 0;
}
