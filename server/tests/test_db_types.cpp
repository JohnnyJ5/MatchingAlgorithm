// GTest suite for db_types.h — DbResult<T>, dbOk/dbValue/dbError helpers,
// DbErrc enum, and domain structs.
//
// No libpq dependency.  These types are the return contract for every
// DBManager method; bugs here silently corrupt all DB-layer error handling.

#include "../db_types.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

// ── dbOk / dbValue ────────────────────────────────────────────────────────────

TEST(DbTypesTest, ValueIsOk) {
    DbResult<int> r = 42;
    EXPECT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r), 42);
}

TEST(DbTypesTest, ErrorIsNotOk) {
    DbResult<int> r = DbError{DbErrc::NotFound, "user not found"};
    EXPECT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
    EXPECT_EQ(dbError(r).message, "user not found");
}

// ── DbResult<std::monostate> ──────────────────────────────────────────────────

// updateUser() and deleteUser() use this for void-like success.
TEST(DbTypesTest, MonostateOk) {
    DbResult<std::monostate> r = std::monostate{};
    EXPECT_TRUE(dbOk(r));
}

TEST(DbTypesTest, MonostateError) {
    DbResult<std::monostate> r = DbError{DbErrc::InvalidInput, "no fields"};
    EXPECT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::InvalidInput);
    EXPECT_EQ(dbError(r).message, "no fields");
}

// ── Complex struct round-trips ────────────────────────────────────────────────

TEST(DbTypesTest, RegisteredUserRoundtrip) {
    RegisteredUser u{99, "bob"};
    DbResult<RegisteredUser> r = u;
    EXPECT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).id, 99);
    EXPECT_EQ(dbValue(r).alias, "bob");
}

TEST(DbTypesTest, LoginResultRoundtrip) {
    LoginResult lr{7, "carol", "admin"};
    DbResult<LoginResult> r = lr;
    EXPECT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).userId, 7);
    EXPECT_EQ(dbValue(r).alias, "carol");
    EXPECT_EQ(dbValue(r).role, "admin");
}

// ── Reassignment ──────────────────────────────────────────────────────────────

// Re-assigning a previously-ok result to an error must flip the state.
// Guards against a result type that caches the original discriminant.
TEST(DbTypesTest, ReassignValueToError) {
    DbResult<int> r = 100;
    EXPECT_TRUE(dbOk(r));
    r = DbError{DbErrc::Conflict, "duplicate"};
    EXPECT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::Conflict);
}

// Re-assigning a previously-error result to a value must flip back.
TEST(DbTypesTest, ReassignErrorToValue) {
    DbResult<std::string> r = DbError{DbErrc::InternalError, "oops"};
    EXPECT_FALSE(dbOk(r));
    r = std::string("recovered");
    EXPECT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r), "recovered");
}

// ── DbErrc pairwise distinctness ──────────────────────────────────────────────

// All DbErrc enum values must be pairwise distinct.
// An accidental duplicate would make error-code comparisons unreliable.
TEST(DbTypesTest, DberrcValuesAllDistinct) {
    const DbErrc vals[] = {
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
            EXPECT_NE(vals[i], vals[j])
                << "DbErrc values at index " << i << " and " << j << " are equal";
}

// ── DbError struct ────────────────────────────────────────────────────────────

TEST(DbTypesTest, DberrorFieldsIndependent) {
    DbError e{DbErrc::Unauthorized, "bad password"};
    EXPECT_EQ(e.code, DbErrc::Unauthorized);
    EXPECT_EQ(e.message, "bad password");
    e.message = "changed";
    EXPECT_EQ(e.code, DbErrc::Unauthorized);  // code must not have changed
}

// ── Vector result ─────────────────────────────────────────────────────────────

// DbResult<vector<CompatibilityScore>> is used by getCompatibilityMatrix().
TEST(DbTypesTest, VectorResultRoundtrip) {
    std::vector<CompatibilityScore> scores = {{1, 2, 85}, {1, 3, 72}};
    DbResult<std::vector<CompatibilityScore>> r = scores;
    EXPECT_TRUE(dbOk(r));
    ASSERT_EQ(dbValue(r).size(), 2u);
    EXPECT_EQ(dbValue(r)[0].score, 85);
    EXPECT_EQ(dbValue(r)[1].manId, 1);
}
