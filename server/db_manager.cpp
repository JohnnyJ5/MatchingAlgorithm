#include "db_manager.h"

#include <optional>
#include <sstream>
#include <string>

// ── Internal RAII helper ──────────────────────────────────────────────────────

// Wraps a PGresult* and calls PQclear on destruction.
// All PQexecParams results must be immediately wrapped in this guard.
namespace {

struct PGResultGuard {
    PGresult* res;
    explicit PGResultGuard(PGresult* r) noexcept : res(r) {}
    PGResultGuard() noexcept : res(nullptr) {}
    ~PGResultGuard() noexcept { if (res) PQclear(res); }

    PGResultGuard(const PGResultGuard&)            = delete;
    PGResultGuard& operator=(const PGResultGuard&) = delete;

    PGResultGuard(PGResultGuard&& o) noexcept : res(o.res) { o.res = nullptr; }
    PGResultGuard& operator=(PGResultGuard&& o) noexcept {
        if (this != &o) { if (res) PQclear(res); res = o.res; o.res = nullptr; }
        return *this;
    }

    PGresult* get() const noexcept { return res; }
};

// Builds a PostgreSQL TEXT[] literal from a vector of strings, e.g. {hiking,jazz}.
std::string buildPgArray(const std::vector<std::string>& items) {
    std::string arr = "{";
    bool first = true;
    for (const auto& item : items) {
        if (!first) arr += ',';
        arr += '"';
        for (char c : item) {
            if (c == '"' || c == '\\') arr += '\\';
            arr += c;
        }
        arr += '"';
        first = false;
    }
    arr += '}';
    return arr;
}

// Returns the value at (row, col) as a std::string, or "" if null.
std::string pgGetStr(PGresult* res, int row, int col) {
    if (PQgetisnull(res, row, col)) return "";
    return std::string(PQgetvalue(res, row, col));
}

// Returns true for PG "t" boolean result.
bool pgGetBool(PGresult* res, int row, int col) {
    return pgGetStr(res, row, col) == "t";
}

} // namespace

// ── Construction ─────────────────────────────────────────────────────────────

DBManager::DBManager(std::string_view connStr, int poolSize)
    : d_pool_(connStr, static_cast<std::size_t>(poolSize))
{}

// ── SQLSTATE mapping ──────────────────────────────────────────────────────────

DbErrc DBManager::mapSqlState(const char* sqlstate) noexcept {
    if (!sqlstate) return DbErrc::InternalError;
    std::string s(sqlstate);
    if (s == "23505") return DbErrc::Conflict;      // unique violation
    if (s == "23503") return DbErrc::InvalidInput;  // foreign key violation
    if (s == "23514") return DbErrc::InvalidInput;  // check constraint
    if (s == "22023") return DbErrc::InvalidInput;  // invalid parameter value
    if (s.substr(0,2) == "08") return DbErrc::InternalError; // connection issues
    return DbErrc::InternalError;
}

// ── User management ───────────────────────────────────────────────────────────

DbResult<RegisteredUser> DBManager::registerUser(
    std::string_view alias,
    std::string_view realName,
    std::string_view email,
    std::string_view password,
    std::string_view gender,
    int age,
    std::string_view bio,
    const std::vector<std::string>& interests)
{
    auto cg = d_pool_.acquire();

    const std::string age_s          = std::to_string(age);
    const std::string interests_arr  = buildPgArray(interests);
    const std::string alias_s(alias);
    const std::string realName_s(realName);
    const std::string email_s(email);
    const std::string password_s(password);
    const std::string gender_s(gender);
    const std::string bio_s(bio);

    const char* params[] = {
        alias_s.c_str(), realName_s.c_str(), email_s.c_str(), password_s.c_str(),
        gender_s.c_str(), age_s.c_str(), bio_s.c_str(), interests_arr.c_str()
    };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "INSERT INTO users (alias, real_name, email, password_hash, gender, age, bio, interests) "
        "VALUES ($1, $2, $3, crypt($4, gen_salt('bf')), $5::gender_type, $6::smallint, "
        "NULLIF($7,''), $8::text[]) "
        "RETURNING id, alias",
        8, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK) {
        const char* sqlstate = PQresultErrorField(pgres.get(), PG_DIAG_SQLSTATE);
        return DbError{mapSqlState(sqlstate), PQerrorMessage(cg.get())};
    }

    return RegisteredUser{
        std::stoll(PQgetvalue(pgres.get(), 0, 0)),
        std::string(PQgetvalue(pgres.get(), 0, 1))
    };
}

DbResult<LoginResult> DBManager::loginUser(
    std::string_view email,
    std::string_view password)
{
    auto cg = d_pool_.acquire();

    const std::string email_s(email);
    const std::string password_s(password);
    const char* params[] = { email_s.c_str(), password_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT id, alias, role FROM users "
        "WHERE email = $1 "
        "  AND password_hash = crypt($2, password_hash) "
        "  AND deleted_at IS NULL",
        2, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK || PQntuples(pgres.get()) == 0) {
        return DbError{DbErrc::Unauthorized, "invalid email or password"};
    }

    return LoginResult{
        std::stoll(PQgetvalue(pgres.get(), 0, 0)),
        std::string(PQgetvalue(pgres.get(), 0, 1)),
        std::string(PQgetvalue(pgres.get(), 0, 2))
    };
}

DbResult<UserListResult> DBManager::listUsers(
    int page, int limit, std::string_view genderFilter)
{
    auto cg = d_pool_.acquire();

    const int offset        = (page - 1) * limit;
    const std::string lim_s = std::to_string(limit);
    const std::string off_s = std::to_string(offset);

    PGResultGuard pgres{nullptr};
    const std::string gender_s(genderFilter);

    if (!gender_s.empty() && (gender_s == "male" || gender_s == "female")) {
        const char* params[] = { gender_s.c_str(), lim_s.c_str(), off_s.c_str() };
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT id, alias, gender, registered_at, has_completed_questionnaire "
            "FROM users WHERE deleted_at IS NULL AND gender = $1::gender_type "
            "ORDER BY registered_at DESC LIMIT $2::int OFFSET $3::int",
            3, nullptr, params, nullptr, nullptr, 0));
    } else {
        const char* params[] = { lim_s.c_str(), off_s.c_str() };
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT id, alias, gender, registered_at, has_completed_questionnaire "
            "FROM users WHERE deleted_at IS NULL "
            "ORDER BY registered_at DESC LIMIT $1::int OFFSET $2::int",
            2, nullptr, params, nullptr, nullptr, 0));
    }

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK) {
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    UserListResult result;
    result.page  = page;
    result.limit = limit;
    const int rows = PQntuples(pgres.get());
    result.users.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        result.users.push_back(UserSummary{
            std::stoll(PQgetvalue(pgres.get(), i, 0)),
            pgGetStr(pgres.get(), i, 1),
            pgGetStr(pgres.get(), i, 2),
            pgGetStr(pgres.get(), i, 3),
            pgGetBool(pgres.get(), i, 4)
        });
    }
    return result;
}

DbResult<UserProfile> DBManager::getUser(int64_t userId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(userId);
    const char* params[] = { id_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT id, alias, gender, age, bio, interests, has_completed_questionnaire "
        "FROM users WHERE id = $1::bigint AND deleted_at IS NULL",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK || PQntuples(pgres.get()) == 0) {
        return DbError{DbErrc::NotFound, "user not found"};
    }

    UserProfile p;
    p.id     = std::stoll(PQgetvalue(pgres.get(), 0, 0));
    p.alias  = pgGetStr(pgres.get(), 0, 1);
    p.gender = pgGetStr(pgres.get(), 0, 2);
    p.age    = std::stoi(PQgetvalue(pgres.get(), 0, 3));
    if (!PQgetisnull(pgres.get(), 0, 4))
        p.bio = pgGetStr(pgres.get(), 0, 4);
    if (!PQgetisnull(pgres.get(), 0, 5))
        p.interests = pgGetStr(pgres.get(), 0, 5);
    p.hasCompletedQuestionnaire = pgGetBool(pgres.get(), 0, 6);
    return p;
}

DbResult<std::monostate> DBManager::updateUser(int64_t userId, const UpdateFields& fields) {
    auto cg = d_pool_.acquire();

    std::vector<std::string> clauses;
    std::vector<std::string> values;

    if (fields.alias)
        clauses.push_back("alias = $" + std::to_string(values.size() + 1)),
        values.push_back(*fields.alias);
    if (fields.bio)
        clauses.push_back("bio = $" + std::to_string(values.size() + 1)),
        values.push_back(*fields.bio);
    if (fields.age)
        clauses.push_back("age = $" + std::to_string(values.size() + 1) + "::smallint"),
        values.push_back(std::to_string(*fields.age));
    if (fields.interests)
        clauses.push_back("interests = $" + std::to_string(values.size() + 1) + "::text[]"),
        values.push_back(buildPgArray(*fields.interests));

    if (clauses.empty())
        return DbError{DbErrc::InvalidInput, "no updatable fields provided"};

    values.push_back(std::to_string(userId));
    const std::string id_placeholder = "$" + std::to_string(values.size());

    std::string sql = "UPDATE users SET ";
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += clauses[i];
    }
    sql += " WHERE id = " + id_placeholder + "::bigint AND deleted_at IS NULL RETURNING id";

    std::vector<const char*> params;
    params.reserve(values.size());
    for (const auto& v : values) params.push_back(v.c_str());

    PGResultGuard pgres(PQexecParams(cg.get(), sql.c_str(),
        static_cast<int>(params.size()), nullptr, params.data(), nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK || PQntuples(pgres.get()) == 0)
        return DbError{DbErrc::NotFound, "user not found or update failed"};

    return std::monostate{};
}

DbResult<std::monostate> DBManager::deleteUser(int64_t userId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(userId);
    const char* params[] = { id_s.c_str() };

    // Step 1: nullify PII to satisfy GDPR right-to-erasure.
    PGResultGuard pgres1(PQexecParams(cg.get(),
        "UPDATE users SET real_name = NULL, email = NULL, password_hash = NULL, "
        "bio = NULL, interests = '{}' "
        "WHERE id = $1::bigint AND deleted_at IS NULL",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres1.get()) != PGRES_COMMAND_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    if (std::string(PQcmdTuples(pgres1.get())) == "0")
        return DbError{DbErrc::NotFound, "user not found"};

    // Step 2: soft-delete.
    PGResultGuard pgres2(PQexecParams(cg.get(),
        "UPDATE users SET deleted_at = NOW() WHERE id = $1::bigint",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres2.get()) != PGRES_COMMAND_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    return std::monostate{};
}

// ── Questionnaire ─────────────────────────────────────────────────────────────

DbResult<QuestionnaireSubmission> DBManager::submitQuestionnaire(
    int64_t userId,
    int questionVersion,
    const std::vector<QuestionnaireAnswer>& answers)
{
    auto cg = d_pool_.acquire();

    // Build JSONB answer array: [{"questionId":1,"answer":"yes"}, ...]
    std::ostringstream json;
    json << '[';
    for (size_t i = 0; i < answers.size(); ++i) {
        if (i > 0) json << ',';
        json << "{\"questionId\":" << answers[i].questionId
             << ",\"answer\":\"" << answers[i].answer << "\"}";
    }
    json << ']';
    const std::string answersJson = json.str();

    const std::string userId_s  = std::to_string(userId);
    const std::string version_s = std::to_string(questionVersion);

    // Run in a transaction so all three steps are atomic.
    PGResultGuard beginRes(PQexec(cg.get(), "BEGIN"));
    if (PQresultStatus(beginRes.get()) != PGRES_COMMAND_OK) {
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    const char* p1[] = { userId_s.c_str() };
    PGResultGuard markOld(PQexecParams(cg.get(),
        "UPDATE questionnaire_submissions SET is_current = false "
        "WHERE user_id = $1::bigint AND is_current = true",
        1, nullptr, p1, nullptr, nullptr, 0));
    if (PQresultStatus(markOld.get()) != PGRES_COMMAND_OK) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    const char* p2[] = { userId_s.c_str(), version_s.c_str(), answersJson.c_str() };
    PGResultGuard insertRes(PQexecParams(cg.get(),
        "INSERT INTO questionnaire_submissions (user_id, question_version, answers, is_current) "
        "VALUES ($1::bigint, $2::int, $3::jsonb, true) "
        "RETURNING id, user_id, submitted_at",
        3, nullptr, p2, nullptr, nullptr, 0));
    if (PQresultStatus(insertRes.get()) != PGRES_TUPLES_OK) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    PGResultGuard markDone(PQexecParams(cg.get(),
        "UPDATE users SET has_completed_questionnaire = true WHERE id = $1::bigint",
        1, nullptr, p1, nullptr, nullptr, 0));
    if (PQresultStatus(markDone.get()) != PGRES_COMMAND_OK) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    PGResultGuard commitRes(PQexec(cg.get(), "COMMIT"));
    if (PQresultStatus(commitRes.get()) != PGRES_COMMAND_OK) {
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    return QuestionnaireSubmission{
        std::stoll(PQgetvalue(insertRes.get(), 0, 0)),
        std::stoll(PQgetvalue(insertRes.get(), 0, 1)),
        pgGetStr(insertRes.get(), 0, 2)
    };
}

// ── Compatibility ─────────────────────────────────────────────────────────────

DbResult<std::vector<CompatibilityScore>> DBManager::getCompatibilityMatrix() {
    auto cg = d_pool_.acquire();

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT man_id, woman_id, score FROM compatibility_scores "
        "ORDER BY man_id, woman_id",
        0, nullptr, nullptr, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    std::vector<CompatibilityScore> result;
    const int rows = PQntuples(pgres.get());
    result.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        result.push_back(CompatibilityScore{
            std::stoll(PQgetvalue(pgres.get(), i, 0)),
            std::stoll(PQgetvalue(pgres.get(), i, 1)),
            std::stoi(PQgetvalue(pgres.get(), i, 2))
        });
    }
    return result;
}

DbResult<std::vector<CompatibilityScore>>
DBManager::getCompatibilityMatrixAboveThreshold(int threshold) {
    auto cg = d_pool_.acquire();

    const std::string thr_s = std::to_string(threshold);
    const char* params[] = { thr_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT man_id, woman_id, score FROM compatibility_scores "
        "WHERE score >= $1::int "
        "ORDER BY man_id, woman_id",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    std::vector<CompatibilityScore> result;
    const int rows = PQntuples(pgres.get());
    result.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        result.push_back(CompatibilityScore{
            std::stoll(PQgetvalue(pgres.get(), i, 0)),
            std::stoll(PQgetvalue(pgres.get(), i, 1)),
            std::stoi(PQgetvalue(pgres.get(), i, 2))
        });
    }
    return result;
}

// ── Events ────────────────────────────────────────────────────────────────────

DbResult<std::vector<Event>> DBManager::listEvents(std::string_view statusFilter) {
    auto cg = d_pool_.acquire();

    const std::string sf(statusFilter);
    PGResultGuard pgres{nullptr};

    if (sf == "all") {
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT e.id, e.name, e.description, e.event_date, e.max_participants, "
            "       COUNT(ep.id) AS registered_count, e.status, e.default_algorithm "
            "FROM events e "
            "LEFT JOIN event_participants ep ON ep.event_id = e.id "
            "GROUP BY e.id ORDER BY e.event_date DESC",
            0, nullptr, nullptr, nullptr, nullptr, 0));
    } else if (sf == "past") {
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT e.id, e.name, e.description, e.event_date, e.max_participants, "
            "       COUNT(ep.id) AS registered_count, e.status, e.default_algorithm "
            "FROM events e "
            "LEFT JOIN event_participants ep ON ep.event_id = e.id "
            "WHERE e.status = 'past' "
            "GROUP BY e.id ORDER BY e.event_date DESC",
            0, nullptr, nullptr, nullptr, nullptr, 0));
    } else {
        // default: upcoming
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT e.id, e.name, e.description, e.event_date, e.max_participants, "
            "       COUNT(ep.id) AS registered_count, e.status, e.default_algorithm "
            "FROM events e "
            "LEFT JOIN event_participants ep ON ep.event_id = e.id "
            "WHERE e.status IN ('upcoming','active') "
            "GROUP BY e.id ORDER BY e.event_date ASC",
            0, nullptr, nullptr, nullptr, nullptr, 0));
    }

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    std::vector<Event> result;
    const int rows = PQntuples(pgres.get());
    result.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        Event ev;
        ev.id               = std::stoll(PQgetvalue(pgres.get(), i, 0));
        ev.name             = pgGetStr(pgres.get(), i, 1);
        if (!PQgetisnull(pgres.get(), i, 2))
            ev.description  = pgGetStr(pgres.get(), i, 2);
        ev.eventDate        = pgGetStr(pgres.get(), i, 3);
        ev.maxParticipants  = std::stoi(PQgetvalue(pgres.get(), i, 4));
        ev.registeredCount  = std::stoi(PQgetvalue(pgres.get(), i, 5));
        ev.status           = pgGetStr(pgres.get(), i, 6);
        ev.defaultAlgorithm = pgGetStr(pgres.get(), i, 7);
        result.push_back(std::move(ev));
    }
    return result;
}

DbResult<Event> DBManager::createEvent(const CreateEventInput& input) {
    auto cg = d_pool_.acquire();

    const std::string max_s  = std::to_string(input.maxParticipants);
    const std::string by_s   = std::to_string(input.createdBy);
    const std::string thr_s  = input.thresholdOverride
                                ? std::to_string(*input.thresholdOverride) : "";
    const char* thr_ptr      = input.thresholdOverride ? thr_s.c_str() : nullptr;

    const char* params[] = {
        input.name.c_str(),
        input.description ? input.description->c_str() : nullptr,
        input.eventDate.c_str(),
        max_s.c_str(),
        input.algorithmType.c_str(),
        thr_ptr,
        by_s.c_str()
    };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "INSERT INTO events "
        "(name, description, event_date, max_participants, default_algorithm, "
        " threshold_override, created_by) "
        "VALUES ($1, $2, $3::timestamptz, $4::int, $5::algorithm_type, "
        "$6::smallint, $7::bigint) "
        "RETURNING id, name, description, event_date, max_participants, "
        "0 AS registered_count, status, default_algorithm",
        7, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK) {
        const char* sqlstate = PQresultErrorField(pgres.get(), PG_DIAG_SQLSTATE);
        return DbError{mapSqlState(sqlstate), PQerrorMessage(cg.get())};
    }

    Event ev;
    ev.id               = std::stoll(PQgetvalue(pgres.get(), 0, 0));
    ev.name             = pgGetStr(pgres.get(), 0, 1);
    if (!PQgetisnull(pgres.get(), 0, 2))
        ev.description  = pgGetStr(pgres.get(), 0, 2);
    ev.eventDate        = pgGetStr(pgres.get(), 0, 3);
    ev.maxParticipants  = std::stoi(PQgetvalue(pgres.get(), 0, 4));
    ev.registeredCount  = 0;
    ev.status           = pgGetStr(pgres.get(), 0, 6);
    ev.defaultAlgorithm = pgGetStr(pgres.get(), 0, 7);
    return ev;
}

DbResult<Event> DBManager::getEvent(int64_t eventId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(eventId);
    const char* params[] = { id_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT e.id, e.name, e.description, e.event_date, e.max_participants, "
        "       COUNT(ep.id) AS registered_count, e.status, e.default_algorithm "
        "FROM events e "
        "LEFT JOIN event_participants ep ON ep.event_id = e.id "
        "WHERE e.id = $1::bigint "
        "GROUP BY e.id",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK || PQntuples(pgres.get()) == 0)
        return DbError{DbErrc::NotFound, "event not found"};

    Event ev;
    ev.id               = std::stoll(PQgetvalue(pgres.get(), 0, 0));
    ev.name             = pgGetStr(pgres.get(), 0, 1);
    if (!PQgetisnull(pgres.get(), 0, 2))
        ev.description  = pgGetStr(pgres.get(), 0, 2);
    ev.eventDate        = pgGetStr(pgres.get(), 0, 3);
    ev.maxParticipants  = std::stoi(PQgetvalue(pgres.get(), 0, 4));
    ev.registeredCount  = std::stoi(PQgetvalue(pgres.get(), 0, 5));
    ev.status           = pgGetStr(pgres.get(), 0, 6);
    ev.defaultAlgorithm = pgGetStr(pgres.get(), 0, 7);
    return ev;
}

DbResult<std::vector<EventParticipant>> DBManager::getEventParticipants(int64_t eventId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(eventId);
    const char* params[] = { id_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT ep.user_id, u.alias, ep.registered_at "
        "FROM event_participants ep "
        "JOIN users u ON u.id = ep.user_id "
        "WHERE ep.event_id = $1::bigint "
        "ORDER BY ep.registered_at ASC",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    std::vector<EventParticipant> result;
    const int rows = PQntuples(pgres.get());
    result.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        result.push_back(EventParticipant{
            std::stoll(PQgetvalue(pgres.get(), i, 0)),
            pgGetStr(pgres.get(), i, 1),
            pgGetStr(pgres.get(), i, 2)
        });
    }
    return result;
}

DbResult<EventRegistration> DBManager::registerForEvent(int64_t eventId, int64_t userId) {
    auto cg = d_pool_.acquire();

    const std::string eid_s = std::to_string(eventId);
    const std::string uid_s = std::to_string(userId);

    PGResultGuard beginRes(PQexec(cg.get(), "BEGIN"));
    if (PQresultStatus(beginRes.get()) != PGRES_COMMAND_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    // Lock the event row and read capacity.
    const char* ep1[] = { eid_s.c_str() };
    PGResultGuard evRes(PQexecParams(cg.get(),
        "SELECT max_participants, status, "
        "       (SELECT COUNT(*) FROM event_participants WHERE event_id = $1::bigint) "
        "FROM events WHERE id = $1::bigint FOR UPDATE",
        1, nullptr, ep1, nullptr, nullptr, 0));

    if (PQresultStatus(evRes.get()) != PGRES_TUPLES_OK || PQntuples(evRes.get()) == 0) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::NotFound, "event not found"};
    }

    const int maxPart   = std::stoi(PQgetvalue(evRes.get(), 0, 0));
    const std::string evStatus = pgGetStr(evRes.get(), 0, 1);
    const int regCount  = std::stoi(PQgetvalue(evRes.get(), 0, 2));

    if (evStatus != "upcoming" && evStatus != "active") {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InvalidInput, "event is not open for registration"};
    }
    if (regCount >= maxPart) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InvalidInput, "event is full"};
    }

    // Check questionnaire completion.
    const char* ep2[] = { uid_s.c_str() };
    PGResultGuard userRes(PQexecParams(cg.get(),
        "SELECT has_completed_questionnaire FROM users "
        "WHERE id = $1::bigint AND deleted_at IS NULL",
        1, nullptr, ep2, nullptr, nullptr, 0));

    if (PQresultStatus(userRes.get()) != PGRES_TUPLES_OK || PQntuples(userRes.get()) == 0) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::NotFound, "user not found"};
    }
    if (!pgGetBool(userRes.get(), 0, 0)) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InvalidInput, "questionnaire not completed"};
    }

    const char* ep3[] = { eid_s.c_str(), uid_s.c_str() };
    PGResultGuard insRes(PQexecParams(cg.get(),
        "INSERT INTO event_participants (event_id, user_id) "
        "VALUES ($1::bigint, $2::bigint) "
        "RETURNING event_id, user_id, registered_at",
        2, nullptr, ep3, nullptr, nullptr, 0));

    if (PQresultStatus(insRes.get()) != PGRES_TUPLES_OK) {
        const char* sqlstate = PQresultErrorField(insRes.get(), PG_DIAG_SQLSTATE);
        PQexec(cg.get(), "ROLLBACK");
        return DbError{mapSqlState(sqlstate), PQerrorMessage(cg.get())};
    }

    PGResultGuard commitRes(PQexec(cg.get(), "COMMIT"));
    if (PQresultStatus(commitRes.get()) != PGRES_COMMAND_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    return EventRegistration{
        std::stoll(PQgetvalue(insRes.get(), 0, 0)),
        std::stoll(PQgetvalue(insRes.get(), 0, 1)),
        pgGetStr(insRes.get(), 0, 2)
    };
}

// ── Algorithm runs ────────────────────────────────────────────────────────────

DbResult<std::vector<AlgorithmRunRecord>> DBManager::getEventAlgorithmRuns(int64_t eventId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(eventId);
    const char* params[] = { id_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "SELECT id, event_id, algorithm, results::text, total_score, "
        "       matched_count, avg_score, ran_at "
        "FROM algorithm_runs "
        "WHERE event_id = $1::bigint AND is_current = true "
        "ORDER BY ran_at DESC",
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    std::vector<AlgorithmRunRecord> result;
    const int rows = PQntuples(pgres.get());
    result.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        result.push_back(AlgorithmRunRecord{
            std::stoll(PQgetvalue(pgres.get(), i, 0)),
            std::stoll(PQgetvalue(pgres.get(), i, 1)),
            pgGetStr(pgres.get(), i, 2),
            pgGetStr(pgres.get(), i, 3),
            std::stoi(PQgetvalue(pgres.get(), i, 4)),
            std::stoi(PQgetvalue(pgres.get(), i, 5)),
            std::stod(PQgetvalue(pgres.get(), i, 6)),
            pgGetStr(pgres.get(), i, 7)
        });
    }
    return result;
}

DbResult<AlgorithmRunRecord> DBManager::persistAlgorithmRun(const PersistRunInput& input) {
    auto cg = d_pool_.acquire();

    const std::string eid_s   = std::to_string(input.eventId);
    const std::string tot_s   = std::to_string(input.totalScore);
    const std::string cnt_s   = std::to_string(input.matchedCount);
    const std::string avg_s   = std::to_string(input.avgScore);
    const std::string thr_s   = input.threshold ? std::to_string(*input.threshold) : "";
    const char* thr_ptr       = input.threshold ? thr_s.c_str() : nullptr;

    PGResultGuard beginRes(PQexec(cg.get(), "BEGIN"));
    if (PQresultStatus(beginRes.get()) != PGRES_COMMAND_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    // Mark previous runs for the same (event, algorithm) as not current.
    const char* p1[] = { eid_s.c_str(), input.algorithm.c_str() };
    PGResultGuard markOld(PQexecParams(cg.get(),
        "UPDATE algorithm_runs SET is_current = false "
        "WHERE event_id = $1::bigint AND algorithm = $2::algorithm_type AND is_current = true",
        2, nullptr, p1, nullptr, nullptr, 0));
    if (PQresultStatus(markOld.get()) != PGRES_COMMAND_OK) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    const char* p2[] = {
        eid_s.c_str(), input.algorithm.c_str(), thr_ptr,
        input.resultsJson.c_str(), tot_s.c_str(), cnt_s.c_str(), avg_s.c_str()
    };
    PGResultGuard insRes(PQexecParams(cg.get(),
        "INSERT INTO algorithm_runs "
        "(event_id, algorithm, threshold, results, total_score, matched_count, avg_score, is_current) "
        "VALUES ($1::bigint, $2::algorithm_type, $3::smallint, $4::jsonb, "
        "$5::int, $6::int, $7::float, true) "
        "RETURNING id, event_id, algorithm, results::text, total_score, "
        "          matched_count, avg_score, ran_at",
        7, nullptr, p2, nullptr, nullptr, 0));

    if (PQresultStatus(insRes.get()) != PGRES_TUPLES_OK) {
        PQexec(cg.get(), "ROLLBACK");
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};
    }

    PGResultGuard commitRes(PQexec(cg.get(), "COMMIT"));
    if (PQresultStatus(commitRes.get()) != PGRES_COMMAND_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    return AlgorithmRunRecord{
        std::stoll(PQgetvalue(insRes.get(), 0, 0)),
        std::stoll(PQgetvalue(insRes.get(), 0, 1)),
        pgGetStr(insRes.get(), 0, 2),
        pgGetStr(insRes.get(), 0, 3),
        std::stoi(PQgetvalue(insRes.get(), 0, 4)),
        std::stoi(PQgetvalue(insRes.get(), 0, 5)),
        std::stod(PQgetvalue(insRes.get(), 0, 6)),
        pgGetStr(insRes.get(), 0, 7)
    };
}

// ── Matches ───────────────────────────────────────────────────────────────────

namespace {
// Populates a Match struct from a 11-column PGresult row.
// Columns: id, event_id, man_id, man_alias, woman_id, woman_alias,
//          score, status, accepted_by_man, accepted_by_woman, created_at
Match rowToMatch(PGresult* res, int row) {
    return Match{
        std::stoll(PQgetvalue(res, row, 0)),
        std::stoll(PQgetvalue(res, row, 1)),
        std::stoll(PQgetvalue(res, row, 2)),
        pgGetStr(res, row, 3),
        std::stoll(PQgetvalue(res, row, 4)),
        pgGetStr(res, row, 5),
        std::stoi(PQgetvalue(res, row, 6)),
        pgGetStr(res, row, 7),
        pgGetBool(res, row, 8),
        pgGetBool(res, row, 9),
        pgGetStr(res, row, 10)
    };
}
} // namespace

static const char* kMatchSelectJoin =
    "SELECT m.id, m.event_id, m.man_id, um.alias AS man_alias, "
    "       m.woman_id, uw.alias AS woman_alias, "
    "       m.score, m.status, m.accepted_by_man, m.accepted_by_woman, m.created_at "
    "FROM matches m "
    "JOIN users um ON um.id = m.man_id "
    "JOIN users uw ON uw.id = m.woman_id ";

DbResult<std::vector<Match>> DBManager::listMatchesForUser(int64_t userId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(userId);
    const char* params[] = { id_s.c_str() };

    const std::string sql = std::string(kMatchSelectJoin) +
        "WHERE m.man_id = $1::bigint OR m.woman_id = $1::bigint "
        "ORDER BY m.created_at DESC";

    PGResultGuard pgres(PQexecParams(cg.get(), sql.c_str(),
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    std::vector<Match> result;
    const int rows = PQntuples(pgres.get());
    result.reserve(rows);
    for (int i = 0; i < rows; ++i)
        result.push_back(rowToMatch(pgres.get(), i));
    return result;
}

DbResult<Match> DBManager::getMatch(int64_t matchId) {
    auto cg = d_pool_.acquire();

    const std::string id_s = std::to_string(matchId);
    const char* params[] = { id_s.c_str() };

    const std::string sql = std::string(kMatchSelectJoin) +
        "WHERE m.id = $1::bigint";

    PGResultGuard pgres(PQexecParams(cg.get(), sql.c_str(),
        1, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK || PQntuples(pgres.get()) == 0)
        return DbError{DbErrc::NotFound, "match not found"};

    return rowToMatch(pgres.get(), 0);
}

DbResult<Match> DBManager::acceptMatch(int64_t matchId, int64_t userId) {
    auto cg = d_pool_.acquire();

    const std::string mid_s = std::to_string(matchId);
    const std::string uid_s = std::to_string(userId);
    const char* params[] = { mid_s.c_str(), uid_s.c_str() };

    // Use CASE WHEN to set the right flag depending on which side userId is.
    // The sync_match_status trigger handles flipping status to 'accepted'.
    const std::string updateSql = std::string(kMatchSelectJoin) +
        "WHERE m.id = $1::bigint";

    PGResultGuard upd(PQexecParams(cg.get(),
        "UPDATE matches SET "
        "  accepted_by_man   = CASE WHEN man_id   = $2::bigint THEN true ELSE accepted_by_man   END, "
        "  accepted_by_woman = CASE WHEN woman_id = $2::bigint THEN true ELSE accepted_by_woman END "
        "WHERE id = $1::bigint "
        "RETURNING id",
        2, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(upd.get()) != PGRES_TUPLES_OK || PQntuples(upd.get()) == 0)
        return DbError{DbErrc::NotFound, "match not found"};

    // Re-fetch to return the updated row with aliases.
    return getMatch(matchId);
}

DbResult<std::monostate> DBManager::declineMatch(int64_t matchId, int64_t userId) {
    auto cg = d_pool_.acquire();

    const std::string mid_s = std::to_string(matchId);
    const std::string uid_s = std::to_string(userId);
    const char* params[] = { mid_s.c_str(), uid_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "UPDATE matches SET status = 'declined', declined_by = $2::bigint "
        "WHERE id = $1::bigint "
        "RETURNING id",
        2, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK || PQntuples(pgres.get()) == 0)
        return DbError{DbErrc::NotFound, "match not found"};

    return std::monostate{};
}

// ── Messages ──────────────────────────────────────────────────────────────────

DbResult<MessagePage> DBManager::getMessages(
    int64_t matchId,
    int limit,
    std::optional<int64_t> beforeId)
{
    auto cg = d_pool_.acquire();

    const std::string mid_s   = std::to_string(matchId);
    // Fetch limit+1 to detect hasMore.
    const std::string lim_s   = std::to_string(limit + 1);

    PGResultGuard pgres{nullptr};

    if (beforeId) {
        const std::string bid_s = std::to_string(*beforeId);
        const char* params[] = { mid_s.c_str(), bid_s.c_str(), lim_s.c_str() };
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT id, match_id, sender_id, text, sent_at "
            "FROM messages "
            "WHERE match_id = $1::bigint AND id < $2::bigint "
            "ORDER BY id DESC LIMIT $3::int",
            3, nullptr, params, nullptr, nullptr, 0));
    } else {
        const char* params[] = { mid_s.c_str(), lim_s.c_str() };
        pgres = PGResultGuard(PQexecParams(cg.get(),
            "SELECT id, match_id, sender_id, text, sent_at "
            "FROM messages "
            "WHERE match_id = $1::bigint "
            "ORDER BY id DESC LIMIT $2::int",
            2, nullptr, params, nullptr, nullptr, 0));
    }

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK)
        return DbError{DbErrc::InternalError, PQerrorMessage(cg.get())};

    const int rows = PQntuples(pgres.get());
    const bool hasMore = (rows > limit);
    const int count    = hasMore ? limit : rows;

    MessagePage page;
    page.hasMore = hasMore;
    page.messages.reserve(count);
    for (int i = 0; i < count; ++i) {
        page.messages.push_back(Message{
            std::stoll(PQgetvalue(pgres.get(), i, 0)),
            std::stoll(PQgetvalue(pgres.get(), i, 1)),
            std::stoll(PQgetvalue(pgres.get(), i, 2)),
            pgGetStr(pgres.get(), i, 3),
            pgGetStr(pgres.get(), i, 4)
        });
    }
    return page;
}

DbResult<Message> DBManager::sendMessage(
    int64_t matchId,
    int64_t senderId,
    std::string_view text)
{
    auto cg = d_pool_.acquire();

    const std::string mid_s = std::to_string(matchId);
    const std::string sid_s = std::to_string(senderId);
    const std::string text_s(text);
    const char* params[] = { mid_s.c_str(), sid_s.c_str(), text_s.c_str() };

    PGResultGuard pgres(PQexecParams(cg.get(),
        "INSERT INTO messages (match_id, sender_id, text) "
        "VALUES ($1::bigint, $2::bigint, $3) "
        "RETURNING id, match_id, sender_id, text, sent_at",
        3, nullptr, params, nullptr, nullptr, 0));

    if (PQresultStatus(pgres.get()) != PGRES_TUPLES_OK) {
        const char* sqlstate = PQresultErrorField(pgres.get(), PG_DIAG_SQLSTATE);
        return DbError{mapSqlState(sqlstate), PQerrorMessage(cg.get())};
    }

    return Message{
        std::stoll(PQgetvalue(pgres.get(), 0, 0)),
        std::stoll(PQgetvalue(pgres.get(), 0, 1)),
        std::stoll(PQgetvalue(pgres.get(), 0, 2)),
        pgGetStr(pgres.get(), 0, 3),
        pgGetStr(pgres.get(), 0, 4)
    };
}
