// db_manager.h — PostgreSQL access layer.
//
// All SQL lives here; server.cpp never touches libpq directly.
// Each method acquires a connection from a small pool, executes the query,
// maps the result to a typed struct, and releases the connection via RAII.
//
// Thread safety: the connection pool is guarded by a mutex + condition variable.
// Each request handler gets its own connection for the duration of its DB call;
// concurrent handlers never share a connection.
#pragma once

#include "db_types.h"

#include <libpq-fe.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

class DBManager {
public:
    // Connects poolSize libpq connections using connStr.
    // Logs a warning for any connection that fails; the server continues with
    // the successfully-connected subset (partial degradation).
    explicit DBManager(std::string_view connStr, int poolSize = 4);

    // Closes all pooled connections via PQfinish.
    ~DBManager();

    DBManager(const DBManager&)            = delete;
    DBManager& operator=(const DBManager&) = delete;

    // ── User management ───────────────────────────────────────────────────────

    // INSERT INTO users … RETURNING id, alias
    // Returns DbErrc::Conflict on SQLSTATE 23505 (duplicate email).
    [[nodiscard]]
    DbResult<RegisteredUser> registerUser(
        std::string_view alias,
        std::string_view realName,
        std::string_view email,
        std::string_view password,
        std::string_view gender,
        int age,
        std::string_view bio,
        const std::vector<std::string>& interests);

    // SELECT id, alias, role WHERE email and password match (bcrypt comparison).
    // Returns DbErrc::Unauthorized when credentials don't match.
    [[nodiscard]]
    DbResult<LoginResult> loginUser(
        std::string_view email,
        std::string_view password);

    // SELECT users list with pagination and optional gender filter.
    [[nodiscard]]
    DbResult<UserListResult> listUsers(
        int page,
        int limit,
        std::string_view genderFilter = "");

    // SELECT single user's public profile by id.
    // Returns DbErrc::NotFound when soft-deleted or not present.
    [[nodiscard]]
    DbResult<UserProfile> getUser(int64_t userId);

    // Dynamic UPDATE SET on whichever UpdateFields are set.
    // Column names are fixed; only values are parameterised.
    // Returns DbErrc::NotFound if the user doesn't exist or is soft-deleted.
    [[nodiscard]]
    DbResult<std::monostate> updateUser(int64_t userId, const UpdateFields& fields);

    // Two-step GDPR erasure: nullify PII columns then set deleted_at.
    // Returns DbErrc::NotFound if already deleted or missing.
    [[nodiscard]]
    DbResult<std::monostate> deleteUser(int64_t userId);

    // ── Questionnaire ─────────────────────────────────────────────────────────

    // Transaction:
    //   1. UPDATE questionnaire_submissions SET is_current=false WHERE user_id=$1
    //   2. INSERT INTO questionnaire_submissions …
    //   3. UPDATE users SET has_completed_questionnaire=true WHERE id=$1
    [[nodiscard]]
    DbResult<QuestionnaireSubmission> submitQuestionnaire(
        int64_t userId,
        int questionVersion,
        const std::vector<QuestionnaireAnswer>& answers);

    // ── Compatibility ─────────────────────────────────────────────────────────

    // SELECT full N×N score matrix ordered by man_id, woman_id.
    [[nodiscard]]
    DbResult<std::vector<CompatibilityScore>> getCompatibilityMatrix();

    // SELECT scores above the given threshold.
    [[nodiscard]]
    DbResult<std::vector<CompatibilityScore>> getCompatibilityMatrixAboveThreshold(int threshold);

    // ── Events ────────────────────────────────────────────────────────────────

    // SELECT events with optional status filter ("upcoming"|"past"|"all").
    [[nodiscard]]
    DbResult<std::vector<Event>> listEvents(std::string_view statusFilter = "upcoming");

    // INSERT INTO events RETURNING full event row.
    [[nodiscard]]
    DbResult<Event> createEvent(const CreateEventInput& input);

    // SELECT single event + participant count.
    // Returns DbErrc::NotFound if event doesn't exist.
    [[nodiscard]]
    DbResult<Event> getEvent(int64_t eventId);

    // SELECT event_participants joined with users for alias.
    [[nodiscard]]
    DbResult<std::vector<EventParticipant>> getEventParticipants(int64_t eventId);

    // Transaction: SELECT COUNT(*) FOR UPDATE to check capacity, then INSERT.
    // Returns DbErrc::Conflict on duplicate registration.
    // Returns DbErrc::InvalidInput if event is full or user hasn't completed
    // the questionnaire.
    [[nodiscard]]
    DbResult<EventRegistration> registerForEvent(int64_t eventId, int64_t userId);

    // ── Algorithm runs ────────────────────────────────────────────────────────

    // SELECT current algorithm run records for an event (all algorithms).
    [[nodiscard]]
    DbResult<std::vector<AlgorithmRunRecord>> getEventAlgorithmRuns(int64_t eventId);

    // Transaction: mark previous run for same (eventId, algorithm) is_current=false,
    // then INSERT the new run.
    [[nodiscard]]
    DbResult<AlgorithmRunRecord> persistAlgorithmRun(const PersistRunInput& input);

    // ── Matches ───────────────────────────────────────────────────────────────

    // SELECT matches where man_id=$1 OR woman_id=$1, joining aliases.
    [[nodiscard]]
    DbResult<std::vector<Match>> listMatchesForUser(int64_t userId);

    // SELECT single match by id, joining aliases.
    [[nodiscard]]
    DbResult<Match> getMatch(int64_t matchId);

    // UPDATE matches: set accepted_by_man or accepted_by_woman based on which
    // side userId belongs to.
    // The sync_match_status DB trigger auto-sets status='accepted' when both flip.
    [[nodiscard]]
    DbResult<Match> acceptMatch(int64_t matchId, int64_t userId);

    // UPDATE matches SET status='declined', declined_by=$2.
    [[nodiscard]]
    DbResult<std::monostate> declineMatch(int64_t matchId, int64_t userId);

    // ── Messages ──────────────────────────────────────────────────────────────

    // SELECT messages WHERE match_id=$1 [AND id < beforeId]
    // ORDER BY id DESC LIMIT limit+1 (hasMore=true if limit+1 rows returned).
    [[nodiscard]]
    DbResult<MessagePage> getMessages(
        int64_t matchId,
        int limit = 50,
        std::optional<int64_t> beforeId = std::nullopt);

    // INSERT INTO messages RETURNING id, sender_id, text, sent_at.
    // Caller must verify match ownership and accepted status before calling.
    [[nodiscard]]
    DbResult<Message> sendMessage(
        int64_t matchId,
        int64_t senderId,
        std::string_view text);

private:
    // RAII guard that returns a connection to the pool on destruction.
    struct ConnGuard {
        DBManager* mgr  = nullptr;
        PGconn*    conn = nullptr;

        ConnGuard(DBManager* m, PGconn* c) noexcept : mgr(m), conn(c) {}
        ~ConnGuard() noexcept { if (mgr && conn) mgr->release(conn); }

        ConnGuard(const ConnGuard&)            = delete;
        ConnGuard& operator=(const ConnGuard&) = delete;

        ConnGuard(ConnGuard&& o) noexcept
            : mgr(o.mgr), conn(o.conn) { o.mgr = nullptr; o.conn = nullptr; }
    };

    // Blocks until a connection is available (5 s timeout).
    // Returns InternalError on timeout.
    [[nodiscard]] ConnGuard acquire();
    void release(PGconn* conn) noexcept;

    // Maps SQLSTATE string → DbErrc for uniform error reporting.
    static DbErrc mapSqlState(const char* sqlstate) noexcept;

    std::vector<PGconn*>    pool_;
    std::queue<PGconn*>     available_;
    std::mutex              poolMutex_;
    std::condition_variable poolCv_;
    std::string             connStr_;
};
