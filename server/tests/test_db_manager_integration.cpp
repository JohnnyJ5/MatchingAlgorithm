// Integration tests for DBManager — requires a live PostgreSQL instance.
//
// Setup (done once before running):
//   createdb spark_test
//   psql spark_test -f db/schema.sql
//   # user spark_test_user / spark_test_pw must exist and be SUPERUSER
//     (superuser bypasses RLS so tests don't need to set session variables)
//
// Each test gets a fresh database via SetUp() which TRUNCATEs all tables.
// The helper adminExec() / adminQuery() use a direct libpq connection for
// data setup that has no DBManager entry point (e.g. inserting raw matches
// or compatibility scores).

#include "../db_manager.h"

#include <gtest/gtest.h>
#include <libpq-fe.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// ── Test connection string ────────────────────────────────────────────────────

static constexpr const char* kTestConnStr =
    "host=127.0.0.1 port=5432 dbname=spark_test "
    "user=spark_test_user password=spark_test_pw connect_timeout=5";

// ── Fixture ───────────────────────────────────────────────────────────────────

class DBManagerIntegrationTest : public ::testing::Test {
public:
    // Shared DBManager — one pool across all tests (connections are reused).
    static DBManager& db() {
        static DBManager instance(kTestConnStr, 3);
        return instance;
    }

    // Direct admin connection for low-level setup / assertions that have no
    // DBManager entry point (inserting matches, compat scores, etc.).
    static PGconn* admin() {
        static PGconn* conn = nullptr;
        if (!conn) {
            conn = PQconnectdb(kTestConnStr);
            if (PQstatus(conn) != CONNECTION_OK)
                throw std::runtime_error(
                    std::string("admin conn failed: ") + PQerrorMessage(conn));
        }
        return conn;
    }

    // Execute SQL that returns no rows; aborts the test on failure.
    static void adminExec(const char* sql) {
        PGresult* r = PQexec(admin(), sql);
        ASSERT_EQ(PQresultStatus(r), PGRES_COMMAND_OK)
            << "adminExec failed: " << PQerrorMessage(admin())
            << "\nSQL: " << sql;
        PQclear(r);
    }

    // Execute a parameterised query; returns the first column of the first row
    // as int64_t.  Aborts the test if the query fails or returns no rows.
    static int64_t adminQueryId(const char* sql,
                                const std::vector<std::string>& args) {
        std::vector<const char*> params;
        for (auto& a : args) params.push_back(a.c_str());
        PGresult* r = PQexecParams(admin(), sql,
            static_cast<int>(params.size()), nullptr,
            params.data(), nullptr, nullptr, 0);
        EXPECT_EQ(PQresultStatus(r), PGRES_TUPLES_OK)
            << PQerrorMessage(admin());
        EXPECT_GT(PQntuples(r), 0) << "adminQueryId: query returned no rows";
        int64_t id = 0;
        if (PQntuples(r) > 0) id = std::stoll(PQgetvalue(r, 0, 0));
        PQclear(r);
        return id;
    }

    // ── Per-test setup ────────────────────────────────────────────────────────

    void SetUp() override {
        // Truncate in dependency order so FK cascades don't fire noisily.
        adminExec(
            "TRUNCATE messages, matches, algorithm_runs, "
            "event_participants, events, compatibility_scores, "
            "questionnaire_submissions, users "
            "RESTART IDENTITY CASCADE");
    }

    // ── Data helpers ──────────────────────────────────────────────────────────

    // Register a user via DBManager and return their id.
    int64_t makeUser(const char* alias, const char* email,
                     const char* gender = "male") {
        auto r = db().registerUser(alias, "Real Name", email,
                                   "password123", gender, 25, "bio", {});
        if (!dbOk(r)) ADD_FAILURE() << "makeUser failed: " << dbError(r).message;
        return dbOk(r) ? dbValue(r).id : -1;
    }

    // Submit questionnaire for a user (marks has_completed_questionnaire=true).
    void completeQuestionnaire(int64_t userId) {
        auto r = db().submitQuestionnaire(userId, 1,
            {{1, "5"}, {2, "3"}, {3, "yes"}});
        if (!dbOk(r)) ADD_FAILURE() << "completeQuestionnaire failed: "
                                    << dbError(r).message;
    }

    // Create an event via DBManager and return its id.
    int64_t makeEvent(const char* name, int maxPart = 20,
                      const char* algo = "hungarian") {
        CreateEventInput inp;
        inp.name            = name;
        inp.eventDate       = "2030-12-01T19:00:00Z";
        inp.maxParticipants = maxPart;
        inp.algorithmType   = algo;
        inp.createdBy       = 0;          // no creator required
        auto r = db().createEvent(inp);
        if (!dbOk(r)) ADD_FAILURE() << "makeEvent failed: " << dbError(r).message;
        return dbOk(r) ? dbValue(r).id : -1;
    }

    // Insert a compatibility score row directly.
    void insertCompatScore(int64_t manId, int64_t womanId, int score) {
        adminQueryId(
            "INSERT INTO compatibility_scores (man_id, woman_id, score) "
            "VALUES ($1::bigint, $2::bigint, $3::smallint) RETURNING id",
            {std::to_string(manId), std::to_string(womanId),
             std::to_string(score)});
    }

    // Persist an algorithm run and return its id.
    int64_t makeAlgorithmRun(int64_t eventId,
                             const char* algo = "hungarian") {
        PersistRunInput inp;
        inp.eventId      = eventId;
        inp.algorithm    = algo;
        inp.resultsJson  = R"({"pairs":[]})";
        inp.totalScore   = 0;
        inp.matchedCount = 0;
        inp.avgScore     = 0.0;
        auto r = db().persistAlgorithmRun(inp);
        if (!dbOk(r)) ADD_FAILURE() << "makeAlgorithmRun failed: "
                                    << dbError(r).message;
        return dbOk(r) ? dbValue(r).id : -1;
    }

    // Insert a pending match directly (no DBManager create-match endpoint).
    int64_t insertMatch(int64_t runId, int64_t eventId,
                        int64_t manId, int64_t womanId, int score = 75) {
        return adminQueryId(
            "INSERT INTO matches "
            "(algorithm_run_id, event_id, man_id, woman_id, score) "
            "VALUES ($1::bigint, $2::bigint, $3::bigint, $4::bigint, $5::smallint) "
            "RETURNING id",
            {std::to_string(runId), std::to_string(eventId),
             std::to_string(manId), std::to_string(womanId),
             std::to_string(score)});
    }

    // Insert a message directly.
    int64_t insertMessage(int64_t matchId, int64_t senderId,
                          const char* text) {
        return adminQueryId(
            "INSERT INTO messages (match_id, sender_id, text) "
            "VALUES ($1::bigint, $2::bigint, $3) RETURNING id",
            {std::to_string(matchId), std::to_string(senderId), text});
    }
};

// ═════════════════════════════════════════════════════════════════════════════
// registerUser
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, RegisterUser_ReturnsIdAndAlias) {
    auto r = db().registerUser("alice", "Real Alice", "alice@test.com",
                               "hunter2", "female", 28, "loves hiking",
                               {"hiking", "jazz"});
    ASSERT_TRUE(dbOk(r));
    EXPECT_GT(dbValue(r).id, 0);
    EXPECT_EQ(dbValue(r).alias, "alice");
}

TEST_F(DBManagerIntegrationTest, RegisterUser_DuplicateEmail_ReturnsConflict) {
    makeUser("alice", "alice@test.com", "female");
    auto r = db().registerUser("alice2", "Real Alice 2", "alice@test.com",
                               "pass", "female", 22, "", {});
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::Conflict);
}

// ═════════════════════════════════════════════════════════════════════════════
// loginUser
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, LoginUser_CorrectCredentials_ReturnsUser) {
    int64_t id = makeUser("bob", "bob@test.com");
    auto r = db().loginUser("bob@test.com", "password123");
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).userId, id);
    EXPECT_EQ(dbValue(r).alias, "bob");
    EXPECT_EQ(dbValue(r).role, "user");
}

TEST_F(DBManagerIntegrationTest, LoginUser_WrongPassword_ReturnsUnauthorized) {
    makeUser("bob", "bob@test.com");
    auto r = db().loginUser("bob@test.com", "wrongpass");
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::Unauthorized);
}

TEST_F(DBManagerIntegrationTest, LoginUser_UnknownEmail_ReturnsUnauthorized) {
    auto r = db().loginUser("nobody@test.com", "pass");
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::Unauthorized);
}

TEST_F(DBManagerIntegrationTest, LoginUser_SoftDeletedUser_ReturnsUnauthorized) {
    int64_t id = makeUser("del", "del@test.com");
    (void)db().deleteUser(id);
    auto r = db().loginUser("del@test.com", "password123");
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::Unauthorized);
}

// ═════════════════════════════════════════════════════════════════════════════
// listUsers
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, ListUsers_ReturnsAllActiveUsers) {
    makeUser("u1", "u1@t.com", "male");
    makeUser("u2", "u2@t.com", "female");
    auto r = db().listUsers(1, 10);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).users.size(), 2u);
}

TEST_F(DBManagerIntegrationTest, ListUsers_ExcludesSoftDeletedUsers) {
    int64_t id = makeUser("gone", "gone@t.com");
    makeUser("here", "here@t.com");
    (void)db().deleteUser(id);
    auto r = db().listUsers(1, 10);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).users.size(), 1u);
    EXPECT_EQ(dbValue(r).users[0].alias, "here");
}

TEST_F(DBManagerIntegrationTest, ListUsers_GenderFilter_ReturnsMalesOnly) {
    makeUser("m1", "m1@t.com", "male");
    makeUser("f1", "f1@t.com", "female");
    auto r = db().listUsers(1, 10, "male");
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).users.size(), 1u);
    EXPECT_EQ(dbValue(r).users[0].gender, "male");
}

TEST_F(DBManagerIntegrationTest, ListUsers_Pagination_RespectsLimitAndPage) {
    for (int i = 0; i < 5; ++i)
        makeUser(("u" + std::to_string(i)).c_str(),
                 ("u" + std::to_string(i) + "@t.com").c_str());
    auto p1 = db().listUsers(1, 3);
    auto p2 = db().listUsers(2, 3);
    ASSERT_TRUE(dbOk(p1));
    ASSERT_TRUE(dbOk(p2));
    EXPECT_EQ(dbValue(p1).users.size(), 3u);
    EXPECT_EQ(dbValue(p2).users.size(), 2u);
    EXPECT_EQ(dbValue(p1).page, 1);
    EXPECT_EQ(dbValue(p2).page, 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// getUser
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, GetUser_Found_ReturnsCorrectFields) {
    int64_t id = makeUser("carol", "carol@t.com", "female");
    auto r = db().getUser(id);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).id,     id);
    EXPECT_EQ(dbValue(r).alias,  "carol");
    EXPECT_EQ(dbValue(r).gender, "female");
    EXPECT_EQ(dbValue(r).age,    25);
    EXPECT_FALSE(dbValue(r).hasCompletedQuestionnaire);
}

TEST_F(DBManagerIntegrationTest, GetUser_NonExistent_ReturnsNotFound) {
    auto r = db().getUser(999999);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
}

TEST_F(DBManagerIntegrationTest, GetUser_SoftDeleted_ReturnsNotFound) {
    int64_t id = makeUser("ghost", "ghost@t.com");
    (void)db().deleteUser(id);
    auto r = db().getUser(id);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
}

// ═════════════════════════════════════════════════════════════════════════════
// updateUser
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, UpdateUser_Alias_ChangesPersists) {
    int64_t id = makeUser("old", "old@t.com");
    UpdateFields f;
    f.alias = "new";
    ASSERT_TRUE(dbOk(db().updateUser(id, f)));
    auto r = db().getUser(id);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).alias, "new");
}

TEST_F(DBManagerIntegrationTest, UpdateUser_AgeAndBio_BothPersist) {
    int64_t id = makeUser("u", "u@t.com");
    UpdateFields f;
    f.age = 30;
    f.bio = "updated bio";
    ASSERT_TRUE(dbOk(db().updateUser(id, f)));
    auto r = db().getUser(id);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).age, 30);
    ASSERT_TRUE(dbValue(r).bio.has_value());
    EXPECT_EQ(*dbValue(r).bio, "updated bio");
}

TEST_F(DBManagerIntegrationTest, UpdateUser_EmptyFields_ReturnsInvalidInput) {
    int64_t id = makeUser("u", "u@t.com");
    auto r = db().updateUser(id, UpdateFields{});
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::InvalidInput);
}

TEST_F(DBManagerIntegrationTest, UpdateUser_NonExistent_ReturnsNotFound) {
    UpdateFields f;
    f.alias = "x";
    auto r = db().updateUser(999999, f);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
}

// ═════════════════════════════════════════════════════════════════════════════
// deleteUser
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, DeleteUser_SoftDeletesAndNullsPii) {
    int64_t id = makeUser("bye", "bye@t.com");
    ASSERT_TRUE(dbOk(db().deleteUser(id)));
    // getUser should return NotFound after soft-delete.
    EXPECT_EQ(dbError(db().getUser(id)).code, DbErrc::NotFound);
    // Verify deleted_at is set and PII is NULL in the raw row.
    const std::string del_id_s = std::to_string(id);
    const char* del_id_p[] = { del_id_s.c_str() };
    PGresult* r = PQexecParams(admin(),
        "SELECT deleted_at, email, real_name FROM users WHERE id = $1::bigint",
        1, nullptr, del_id_p, nullptr, nullptr, 0);
    ASSERT_EQ(PQresultStatus(r), PGRES_TUPLES_OK);
    ASSERT_EQ(PQntuples(r), 1);
    EXPECT_FALSE(PQgetisnull(r, 0, 0)) << "deleted_at should be set";
    EXPECT_TRUE(PQgetisnull(r, 0, 1)) << "email should be NULL after erasure";
    EXPECT_TRUE(PQgetisnull(r, 0, 2)) << "real_name should be NULL after erasure";
    PQclear(r);
}

TEST_F(DBManagerIntegrationTest, DeleteUser_AlreadyDeleted_ReturnsNotFound) {
    int64_t id = makeUser("bye2", "bye2@t.com");
    (void)db().deleteUser(id);
    auto r = db().deleteUser(id);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
}

// ═════════════════════════════════════════════════════════════════════════════
// submitQuestionnaire
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, SubmitQuestionnaire_SetsHasCompleted) {
    int64_t id = makeUser("q1", "q1@t.com");
    auto r = db().submitQuestionnaire(id, 1, {{1,"5"},{2,"3"}});
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).userId, id);
    EXPECT_GT(dbValue(r).submissionId, 0);
    // getUser should now report hasCompletedQuestionnaire=true.
    auto ur = db().getUser(id);
    ASSERT_TRUE(dbOk(ur));
    EXPECT_TRUE(dbValue(ur).hasCompletedQuestionnaire);
}

TEST_F(DBManagerIntegrationTest, SubmitQuestionnaire_Resubmit_MarksOldNotCurrent) {
    int64_t id = makeUser("q2", "q2@t.com");
    auto r1 = db().submitQuestionnaire(id, 1, {{1,"5"}});
    ASSERT_TRUE(dbOk(r1));
    auto r2 = db().submitQuestionnaire(id, 1, {{1,"7"}});
    ASSERT_TRUE(dbOk(r2));
    // Only one current submission should exist.
    const std::string qs_id_s = std::to_string(id);
    const char* qs_id_p[] = { qs_id_s.c_str() };
    PGresult* r = PQexecParams(admin(),
        "SELECT COUNT(*) FROM questionnaire_submissions "
        "WHERE user_id = $1::bigint AND is_current = true",
        1, nullptr, qs_id_p, nullptr, nullptr, 0);
    ASSERT_EQ(PQresultStatus(r), PGRES_TUPLES_OK);
    EXPECT_EQ(std::string(PQgetvalue(r, 0, 0)), "1");
    PQclear(r);
}

// ═════════════════════════════════════════════════════════════════════════════
// getCompatibilityMatrix / getCompatibilityMatrixAboveThreshold
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, GetCompatibilityMatrix_Empty_ReturnsEmptyVector) {
    auto r = db().getCompatibilityMatrix();
    ASSERT_TRUE(dbOk(r));
    EXPECT_TRUE(dbValue(r).empty());
}

TEST_F(DBManagerIntegrationTest, GetCompatibilityMatrix_ReturnsAllRows) {
    int64_t m1 = makeUser("m1", "m1@t.com", "male");
    int64_t m2 = makeUser("m2", "m2@t.com", "male");
    int64_t w1 = makeUser("w1", "w1@t.com", "female");
    insertCompatScore(m1, w1, 80);
    insertCompatScore(m2, w1, 55);
    auto r = db().getCompatibilityMatrix();
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).size(), 2u);
    // Ordered by man_id, woman_id.
    EXPECT_EQ(dbValue(r)[0].manId, m1);
    EXPECT_EQ(dbValue(r)[0].score, 80);
    EXPECT_EQ(dbValue(r)[1].manId, m2);
    EXPECT_EQ(dbValue(r)[1].score, 55);
}

TEST_F(DBManagerIntegrationTest, GetCompatibilityMatrixAboveThreshold_FiltersCorrectly) {
    int64_t m1 = makeUser("m1", "m1@t.com", "male");
    int64_t w1 = makeUser("w1", "w1@t.com", "female");
    int64_t w2 = makeUser("w2", "w2@t.com", "female");
    insertCompatScore(m1, w1, 80);
    insertCompatScore(m1, w2, 40);
    auto r = db().getCompatibilityMatrixAboveThreshold(65);
    ASSERT_TRUE(dbOk(r));
    ASSERT_EQ(dbValue(r).size(), 1u);
    EXPECT_EQ(dbValue(r)[0].score, 80);
    EXPECT_EQ(dbValue(r)[0].womanId, w1);
}

// ═════════════════════════════════════════════════════════════════════════════
// listEvents / createEvent / getEvent
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, CreateEvent_ReturnsCorrectFields) {
    CreateEventInput inp;
    inp.name            = "Valentine's Night";
    inp.description     = "Speed dating event";
    inp.eventDate       = "2030-02-14T19:00:00Z";
    inp.maxParticipants = 30;
    inp.algorithmType   = "gale-shapley";
    inp.createdBy       = 0;
    auto r = db().createEvent(inp);
    ASSERT_TRUE(dbOk(r));
    EXPECT_GT(dbValue(r).id, 0);
    EXPECT_EQ(dbValue(r).name, "Valentine's Night");
    EXPECT_EQ(dbValue(r).maxParticipants, 30);
    EXPECT_EQ(dbValue(r).defaultAlgorithm, "gale-shapley");
    EXPECT_EQ(dbValue(r).status, "upcoming");
    EXPECT_EQ(dbValue(r).registeredCount, 0);
}

TEST_F(DBManagerIntegrationTest, ListEvents_DefaultFilter_ReturnsUpcoming) {
    int64_t evId = makeEvent("Upcoming");
    // Insert a past event directly.
    adminExec("INSERT INTO events (name, event_date, max_participants, status) "
              "VALUES ('Past', '2000-01-01', 10, 'past')");
    auto r = db().listEvents();
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).size(), 1u);
    EXPECT_EQ(dbValue(r)[0].id, evId);
}

TEST_F(DBManagerIntegrationTest, ListEvents_PastFilter_ReturnsPastOnly) {
    makeEvent("Upcoming");
    adminExec("INSERT INTO events (name, event_date, max_participants, status) "
              "VALUES ('Past', '2000-01-01', 10, 'past')");
    auto r = db().listEvents("past");
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).size(), 1u);
    EXPECT_EQ(dbValue(r)[0].status, "past");
}

TEST_F(DBManagerIntegrationTest, ListEvents_AllFilter_ReturnsEveryEvent) {
    makeEvent("Upcoming");
    adminExec("INSERT INTO events (name, event_date, max_participants, status) "
              "VALUES ('Past', '2000-01-01', 10, 'past')");
    auto r = db().listEvents("all");
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).size(), 2u);
}

TEST_F(DBManagerIntegrationTest, GetEvent_Found_ReturnsEvent) {
    int64_t id = makeEvent("Test Event");
    auto r = db().getEvent(id);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).id, id);
    EXPECT_EQ(dbValue(r).name, "Test Event");
}

TEST_F(DBManagerIntegrationTest, GetEvent_NotFound_ReturnsNotFound) {
    auto r = db().getEvent(999999);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
}

// ═════════════════════════════════════════════════════════════════════════════
// registerForEvent / getEventParticipants
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, RegisterForEvent_HappyPath_Succeeds) {
    int64_t uid = makeUser("reg", "reg@t.com");
    completeQuestionnaire(uid);
    int64_t eid = makeEvent("Event");
    auto r = db().registerForEvent(eid, uid);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).eventId, eid);
    EXPECT_EQ(dbValue(r).userId, uid);
}

TEST_F(DBManagerIntegrationTest, RegisterForEvent_Duplicate_ReturnsConflict) {
    int64_t uid = makeUser("dup", "dup@t.com");
    completeQuestionnaire(uid);
    int64_t eid = makeEvent("Event");
    (void)db().registerForEvent(eid, uid);
    auto r = db().registerForEvent(eid, uid);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::Conflict);
}

TEST_F(DBManagerIntegrationTest, RegisterForEvent_NoQuestionnaire_ReturnsInvalidInput) {
    int64_t uid = makeUser("nq", "nq@t.com");   // questionnaire not completed
    int64_t eid = makeEvent("Event");
    auto r = db().registerForEvent(eid, uid);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::InvalidInput);
}

TEST_F(DBManagerIntegrationTest, RegisterForEvent_EventFull_ReturnsInvalidInput) {
    int64_t eid = makeEvent("Tiny", 1);   // max_participants = 1
    int64_t u1  = makeUser("a", "a@t.com");
    int64_t u2  = makeUser("b", "b@t.com");
    completeQuestionnaire(u1);
    completeQuestionnaire(u2);
    ASSERT_TRUE(dbOk(db().registerForEvent(eid, u1)));
    auto r = db().registerForEvent(eid, u2);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::InvalidInput);
}

TEST_F(DBManagerIntegrationTest, GetEventParticipants_ReturnsRegisteredUsers) {
    int64_t uid = makeUser("p1", "p1@t.com");
    completeQuestionnaire(uid);
    int64_t eid = makeEvent("Event");
    (void)db().registerForEvent(eid, uid);
    auto r = db().getEventParticipants(eid);
    ASSERT_TRUE(dbOk(r));
    ASSERT_EQ(dbValue(r).size(), 1u);
    EXPECT_EQ(dbValue(r)[0].userId, uid);
    EXPECT_EQ(dbValue(r)[0].alias, "p1");
}

// ═════════════════════════════════════════════════════════════════════════════
// persistAlgorithmRun / getEventAlgorithmRuns
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, PersistAlgorithmRun_ReturnsRunRecord) {
    int64_t eid = makeEvent("E");
    PersistRunInput inp;
    inp.eventId      = eid;
    inp.algorithm    = "hungarian";
    inp.resultsJson  = R"({"pairs":[{"manId":1,"womanId":2}]})";
    inp.totalScore   = 85;
    inp.matchedCount = 1;
    inp.avgScore     = 85.0;
    auto r = db().persistAlgorithmRun(inp);
    ASSERT_TRUE(dbOk(r));
    EXPECT_GT(dbValue(r).id, 0);
    EXPECT_EQ(dbValue(r).eventId, eid);
    EXPECT_EQ(dbValue(r).algorithm, "hungarian");
    EXPECT_EQ(dbValue(r).totalScore, 85);
    EXPECT_EQ(dbValue(r).matchedCount, 1);
}

TEST_F(DBManagerIntegrationTest, PersistAlgorithmRun_SecondRun_MarksFirstNotCurrent) {
    int64_t eid = makeEvent("E");
    int64_t run1 = makeAlgorithmRun(eid, "hungarian");
    makeAlgorithmRun(eid, "hungarian");   // second run for same event+algo
    // Verify only the latest is current.
    const std::string ar_eid_s = std::to_string(eid);
    const char* ar_eid_p[] = { ar_eid_s.c_str() };
    PGresult* r = PQexecParams(admin(),
        "SELECT COUNT(*) FROM algorithm_runs "
        "WHERE event_id = $1::bigint AND algorithm = 'hungarian' AND is_current = true",
        1, nullptr, ar_eid_p, nullptr, nullptr, 0);
    ASSERT_EQ(PQresultStatus(r), PGRES_TUPLES_OK);
    EXPECT_EQ(std::string(PQgetvalue(r, 0, 0)), "1");
    PQclear(r);
    (void)run1;
}

TEST_F(DBManagerIntegrationTest, GetEventAlgorithmRuns_ReturnsOnlyCurrentRuns) {
    int64_t eid = makeEvent("E");
    makeAlgorithmRun(eid, "hungarian");
    makeAlgorithmRun(eid, "hungarian");   // replaces previous
    makeAlgorithmRun(eid, "blossom");
    auto r = db().getEventAlgorithmRuns(eid);
    ASSERT_TRUE(dbOk(r));
    // One current per algorithm → 2 runs.
    EXPECT_EQ(dbValue(r).size(), 2u);
}

// ═════════════════════════════════════════════════════════════════════════════
// getMatch / listMatchesForUser / acceptMatch / declineMatch
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, GetMatch_Found_ReturnsAliases) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid, 88);
    auto r = db().getMatch(matchId);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).id,        matchId);
    EXPECT_EQ(dbValue(r).manId,     mid);
    EXPECT_EQ(dbValue(r).womanId,   wid);
    EXPECT_EQ(dbValue(r).manAlias,  "man");
    EXPECT_EQ(dbValue(r).womanAlias,"woman");
    EXPECT_EQ(dbValue(r).score,     88);
    EXPECT_EQ(dbValue(r).status,    "pending");
}

TEST_F(DBManagerIntegrationTest, GetMatch_NotFound_ReturnsNotFound) {
    auto r = db().getMatch(999999);
    ASSERT_FALSE(dbOk(r));
    EXPECT_EQ(dbError(r).code, DbErrc::NotFound);
}

TEST_F(DBManagerIntegrationTest, ListMatchesForUser_ReturnsMatchesForBothSides) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    insertMatch(rid, eid, mid, wid);
    auto rm = db().listMatchesForUser(mid);
    auto rw = db().listMatchesForUser(wid);
    ASSERT_TRUE(dbOk(rm));
    ASSERT_TRUE(dbOk(rw));
    EXPECT_EQ(dbValue(rm).size(), 1u);
    EXPECT_EQ(dbValue(rw).size(), 1u);
}

TEST_F(DBManagerIntegrationTest, ListMatchesForUser_NoMatches_ReturnsEmpty) {
    int64_t uid = makeUser("lone", "lone@t.com");
    auto r = db().listMatchesForUser(uid);
    ASSERT_TRUE(dbOk(r));
    EXPECT_TRUE(dbValue(r).empty());
}

TEST_F(DBManagerIntegrationTest, AcceptMatch_ManSide_SetsManFlag) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    auto r = db().acceptMatch(matchId, mid);
    ASSERT_TRUE(dbOk(r));
    EXPECT_TRUE(dbValue(r).acceptedByMan);
    EXPECT_FALSE(dbValue(r).acceptedByWoman);
    EXPECT_EQ(dbValue(r).status, "pending");
}

TEST_F(DBManagerIntegrationTest, AcceptMatch_BothSides_TriggerSetsAccepted) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    (void)db().acceptMatch(matchId, mid);
    auto r = db().acceptMatch(matchId, wid);
    ASSERT_TRUE(dbOk(r));
    EXPECT_TRUE(dbValue(r).acceptedByMan);
    EXPECT_TRUE(dbValue(r).acceptedByWoman);
    // sync_match_status trigger must have fired.
    EXPECT_EQ(dbValue(r).status, "accepted");
}

TEST_F(DBManagerIntegrationTest, DeclineMatch_SetsDeclinedStatus) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    ASSERT_TRUE(dbOk(db().declineMatch(matchId, mid)));
    // Verify via getMatch.
    auto r = db().getMatch(matchId);
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).status, "declined");
}

// ═════════════════════════════════════════════════════════════════════════════
// sendMessage / getMessages
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(DBManagerIntegrationTest, SendMessage_PersistsText) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    // Accept both sides so status=accepted (required by business logic).
    (void)db().acceptMatch(matchId, mid);
    (void)db().acceptMatch(matchId, wid);

    auto r = db().sendMessage(matchId, mid, "Hello!");
    ASSERT_TRUE(dbOk(r));
    EXPECT_GT(dbValue(r).id, 0);
    EXPECT_EQ(dbValue(r).matchId,  matchId);
    EXPECT_EQ(dbValue(r).senderId, mid);
    EXPECT_EQ(dbValue(r).text,     "Hello!");
}

TEST_F(DBManagerIntegrationTest, GetMessages_Empty_ReturnsEmptyPage) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    auto r = db().getMessages(matchId, 50);
    ASSERT_TRUE(dbOk(r));
    EXPECT_TRUE(dbValue(r).messages.empty());
    EXPECT_FALSE(dbValue(r).hasMore);
}

TEST_F(DBManagerIntegrationTest, GetMessages_ReturnsInReverseChronologicalOrder) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    int64_t msg1 = insertMessage(matchId, mid, "first");
    int64_t msg2 = insertMessage(matchId, wid, "second");
    int64_t msg3 = insertMessage(matchId, mid, "third");
    auto r = db().getMessages(matchId, 50);
    ASSERT_TRUE(dbOk(r));
    ASSERT_EQ(dbValue(r).messages.size(), 3u);
    // ORDER BY id DESC → newest first.
    EXPECT_EQ(dbValue(r).messages[0].id, msg3);
    EXPECT_EQ(dbValue(r).messages[2].id, msg1);
    EXPECT_FALSE(dbValue(r).hasMore);
    (void)msg2;
}

TEST_F(DBManagerIntegrationTest, GetMessages_HasMore_WhenResultsExceedLimit) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    for (int i = 0; i < 5; ++i)
        insertMessage(matchId, mid, ("msg" + std::to_string(i)).c_str());
    auto r = db().getMessages(matchId, 3);   // limit=3, but 5 exist
    ASSERT_TRUE(dbOk(r));
    EXPECT_EQ(dbValue(r).messages.size(), 3u);
    EXPECT_TRUE(dbValue(r).hasMore);
}

TEST_F(DBManagerIntegrationTest, GetMessages_BeforeCursor_PaginatesCorrectly) {
    int64_t mid = makeUser("man",   "man@t.com",   "male");
    int64_t wid = makeUser("woman", "woman@t.com", "female");
    int64_t eid = makeEvent("E");
    int64_t rid = makeAlgorithmRun(eid);
    int64_t matchId = insertMatch(rid, eid, mid, wid);
    for (int i = 0; i < 4; ++i)
        insertMessage(matchId, mid, ("m" + std::to_string(i)).c_str());

    // First page: latest 2.
    auto p1 = db().getMessages(matchId, 2);
    ASSERT_TRUE(dbOk(p1));
    ASSERT_EQ(dbValue(p1).messages.size(), 2u);
    int64_t cursor = dbValue(p1).messages.back().id;

    // Second page: 2 messages before the cursor.
    auto p2 = db().getMessages(matchId, 2, cursor);
    ASSERT_TRUE(dbOk(p2));
    EXPECT_EQ(dbValue(p2).messages.size(), 2u);
    // All messages on page 2 must be older than the cursor.
    for (const auto& msg : dbValue(p2).messages)
        EXPECT_LT(msg.id, cursor);
}
