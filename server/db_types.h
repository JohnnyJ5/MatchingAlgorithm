// db_types.h — Domain structs and result types shared between db_manager and server.
// No libpq dependency: safe to include anywhere.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// ── Error type ────────────────────────────────────────────────────────────────

enum class DbErrc {
    Ok,
    NotFound,       // → HTTP 404
    Conflict,       // → HTTP 409  (e.g. duplicate email, SQLSTATE 23505)
    Unauthorized,   // → HTTP 401  (bad credentials)
    InvalidInput,   // → HTTP 400  (constraint violation the caller can address)
    InternalError,  // → HTTP 500
};

struct DbError {
    DbErrc      code;
    std::string message;   // suitable for server logs; not necessarily for HTTP bodies
};

template<typename T>
using DbResult = std::variant<T, DbError>;

template<typename T>
[[nodiscard]] bool dbOk(const DbResult<T>& r) {
    return std::holds_alternative<T>(r);
}

template<typename T>
[[nodiscard]] const T& dbValue(const DbResult<T>& r) {
    return std::get<T>(r);
}

template<typename T>
[[nodiscard]] const DbError& dbError(const DbResult<T>& r) {
    return std::get<DbError>(r);
}

// ── User structs ──────────────────────────────────────────────────────────────

struct RegisteredUser {
    int64_t     id;
    std::string alias;
};

struct LoginResult {
    int64_t     userId;
    std::string alias;
    std::string role;
};

struct UserSummary {
    int64_t     id;
    std::string alias;
    std::string gender;
    std::string registeredAt;
    bool        hasCompletedQuestionnaire;
};

struct UserProfile {
    int64_t     id;
    std::string alias;
    std::string gender;
    int         age;
    std::optional<std::string> bio;
    // Raw PostgreSQL TEXT[] literal e.g. "{hiking,jazz}". server.cpp parses to JSON array.
    std::optional<std::string> interests;
    bool        hasCompletedQuestionnaire;
};

struct UserListResult {
    std::vector<UserSummary> users;
    int page;
    int limit;
};

struct UpdateFields {
    std::optional<std::string>              alias;
    std::optional<std::string>              bio;
    std::optional<int>                      age;
    // Each element is an interest string; db_manager serialises to PG TEXT[] literal.
    std::optional<std::vector<std::string>> interests;
};

// ── Questionnaire structs ─────────────────────────────────────────────────────

struct QuestionnaireAnswer {
    int64_t     questionId;
    std::string answer;   // covers int/scale/text answer types
};

struct QuestionnaireSubmission {
    int64_t     submissionId;
    int64_t     userId;
    std::string submittedAt;
};

// ── Compatibility structs ─────────────────────────────────────────────────────

struct CompatibilityScore {
    int64_t manId;
    int64_t womanId;
    int     score;
};

// ── Event structs ─────────────────────────────────────────────────────────────

struct Event {
    int64_t     id;
    std::string name;
    std::optional<std::string> description;
    std::string eventDate;
    int         maxParticipants;
    int         registeredCount;
    std::string status;
    std::string defaultAlgorithm;
};

struct EventParticipant {
    int64_t     userId;
    std::string alias;
    std::string registeredAt;
};

struct EventRegistration {
    int64_t     eventId;
    int64_t     userId;
    std::string registeredAt;
};

struct CreateEventInput {
    std::string name;
    std::optional<std::string> description;
    std::string eventDate;
    int         maxParticipants;
    std::string algorithmType;
    std::optional<int> thresholdOverride;
    int64_t     createdBy;
};

// ── Algorithm run structs ─────────────────────────────────────────────────────

struct AlgorithmRunRecord {
    int64_t     id;
    int64_t     eventId;
    std::string algorithm;
    std::string resultsJson;   // JSONB blob as string; server.cpp parses
    int         totalScore;
    int         matchedCount;
    double      avgScore;
    std::string ranAt;
};

struct PersistRunInput {
    int64_t     eventId;
    std::string algorithm;
    std::optional<int> threshold;
    std::string resultsJson;
    int         totalScore;
    int         matchedCount;
    double      avgScore;
};

// ── Match structs ─────────────────────────────────────────────────────────────

struct Match {
    int64_t     id;
    int64_t     eventId;
    int64_t     manId;
    std::string manAlias;
    int64_t     womanId;
    std::string womanAlias;
    int         score;
    std::string status;
    bool        acceptedByMan;
    bool        acceptedByWoman;
    std::string createdAt;
};

// ── Message structs ───────────────────────────────────────────────────────────

struct Message {
    int64_t     id;
    int64_t     matchId;
    int64_t     senderId;
    std::string text;
    std::string sentAt;
};

struct MessagePage {
    std::vector<Message> messages;
    bool                 hasMore;
};
