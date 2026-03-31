// ============================================================================
// Blind Dating Website — REST API Server (Crow)
//
// Endpoints overview:
//
//  STATIC / UI
//    GET  /                        Serve the main SPA (index.html)
//    GET  /static/<file>           Serve static assets (CSS, JS, images)
//
//  USER MANAGEMENT
//    POST   /api/users/register    Register a new participant
//    POST   /api/users/login       Authenticate and return a session token
//    GET    /api/users             List all participants (admin only)
//    GET    /api/users/:id         Get a participant's public profile
//    PUT    /api/users/:id         Update profile fields (name, bio, prefs)
//    DELETE /api/users/:id         Remove an account
//
//  COMPATIBILITY QUESTIONNAIRE
//    POST /api/questionnaire/:id   Submit answers; server computes scores
//    GET  /api/compatibility       Return the full score matrix (admin/debug)
//
//  MATCHING ALGORITHMS  ← these are the buttons in the UI
//    POST /api/algorithms/gale-shapley    Run Gale-Shapley; returns stable pairs
//    POST /api/algorithms/hopcroft-karp  Run Hopcroft-Karp; max compatible pairs
//    POST /api/algorithms/hungarian       Run Hungarian; globally optimal score
//    POST /api/algorithms/blossom         Run Blossom; general-graph max matching
//    GET  /api/algorithms/compare         Run all four and return side-by-side
//
//  MATCHES
//    GET  /api/matches             List all current matches for the session user
//    GET  /api/matches/:id         Details of a single match
//    POST /api/matches/:id/accept  Accept (confirm interest in) a match
//    POST /api/matches/:id/decline Decline a match; removes the pairing
//
//  EVENTS (speed-dating rounds)
//    GET  /api/events              List upcoming blind-dating events
//    POST /api/events              Create a new event (admin)
//    GET  /api/events/:id          Event details + participant list
//    POST /api/events/:id/register Join an event
//    GET  /api/events/:id/results  Get algorithm results for a specific event
//
//  MESSAGES (only between accepted matches)
//    GET  /api/messages/:matchId   Fetch message history for a match
//    POST /api/messages/:matchId   Send a message to a matched partner
// ============================================================================

#include "crow.h"

#include "../src/gale_shapley.h"
#include "../src/hopcroft_karp.h"
#include "../src/hungarian.h"
#include "../src/blossom.h"

#include <libpq-fe.h>
#include <pqxx/pqxx>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ── In-memory dataset (mirrors main.cpp; replace with a real DB in production)

static const std::vector<std::string> MEN   = {"Aaron","Ben","Carlos","Dan","Evan","Frank"};
static const std::vector<std::string> WOMEN = {"Amy","Beth","Clara","Dana","Elena","Fiona"};
static const int N = static_cast<int>(MEN.size());
static const int THRESHOLD = 65;

// COMPAT[man][woman] — questionnaire-derived compatibility score (0-100).
static const std::vector<std::vector<int>> COMPAT = {
    //       Amy  Beth Clara Dana Elena Fiona
    /* Aaron  */ {85, 60,  70,  45,  90,  55},
    /* Ben    */ {70, 85,  55,  80,  40,  65},
    /* Carlos */ {45, 70,  90,  60,  75,  50},
    /* Dan    */ {60, 50,  40,  95,  65,  80},
    /* Evan   */ {90, 40,  65,  55,  50,  70},
    /* Frank  */ {55, 75,  80,  70,  60,  85},
};

// ── Helpers ──────────────────────────────────────────────────────────────────

// Build a JSON array describing the matches for UI consumption.
// matchM[man_index] = woman_index, or -1 if unmatched.
static crow::json::wvalue matchesToJson(const std::vector<int>& matchM)
{
    crow::json::wvalue result;
    crow::json::wvalue::list pairs;
    int totalScore = 0, matchedCount = 0;

    for (int m = 0; m < N; ++m) {
        crow::json::wvalue pair;
        pair["man"] = MEN[m];
        pair["manIndex"] = m;
        if (matchM[m] == -1) {
            pair["woman"]       = nullptr;
            pair["womanIndex"]  = -1;
            pair["score"]       = 0;
            pair["matched"]     = false;
        } else {
            int w = matchM[m];
            int s = COMPAT[m][w];
            totalScore += s;
            ++matchedCount;
            pair["woman"]       = WOMEN[w];
            pair["womanIndex"]  = w;
            pair["score"]       = s;
            pair["matched"]     = true;
        }
        pairs.push_back(std::move(pair));
    }

    result["pairs"]        = std::move(pairs);
    result["totalScore"]   = totalScore;
    result["matchedCount"] = matchedCount;
    result["avgScore"]     = matchedCount > 0
                             ? static_cast<double>(totalScore) / matchedCount
                             : 0.0;
    return result;
}

// Derive ordered preference lists from the compatibility matrix (desc score).
static std::vector<std::vector<int>> buildMenPrefs()
{
    std::vector<std::vector<int>> prefs(N);
    for (int m = 0; m < N; ++m) {
        prefs[m].resize(N);
        std::iota(prefs[m].begin(), prefs[m].end(), 0);
        std::sort(prefs[m].begin(), prefs[m].end(),
                  [&](int a, int b){ return COMPAT[m][a] > COMPAT[m][b]; });
    }
    return prefs;
}

static std::vector<std::vector<int>> buildWomenPrefs()
{
    std::vector<std::vector<int>> prefs(N);
    for (int w = 0; w < N; ++w) {
        prefs[w].resize(N);
        std::iota(prefs[w].begin(), prefs[w].end(), 0);
        std::sort(prefs[w].begin(), prefs[w].end(),
                  [&](int a, int b){ return COMPAT[a][w] > COMPAT[b][w]; });
    }
    return prefs;
}

// Returns the value of an environment variable, or a default if not set.
static std::string getenv_or(const char* name, const char* fallback)
{
    const char* v = std::getenv(name);
    return v ? v : fallback;
}

// ── pqxx helpers ─────────────────────────────────────────────────────────────
// Each Crow handler runs in its own thread. pqxx::connection is not thread-safe
// so each request opens its own connection. Concurrency is handled at the DB
// level via transaction isolation, not with application-side mutexes.

static std::string pqConnStr()
{
    return "host="     + getenv_or("DB_HOST",     "db")   +
           " port="    + getenv_or("DB_PORT",     "5432") +
           " dbname="  + getenv_or("DB_NAME",     "")     +
           " user="    + getenv_or("DB_USER",     "")     +
           " password="+ getenv_or("DB_PASSWORD", "")     +
           " sslmode=" + getenv_or("DB_SSLMODE",  "prefer");
}

// Set session-level RLS variables so policies can identify the caller.
static void setSessionRole(pqxx::transaction_base& txn,
                           const std::string& role,
                           long long userId)
{
    txn.exec("SET LOCAL app.current_role = " + txn.quote(role));
    txn.exec("SET LOCAL app.current_user_id = " + txn.quote(std::to_string(userId)));
}

// ── Scoring helper ────────────────────────────────────────────────────────────
// Weighted Euclidean distance between two answer vectors; normalised to 0-100.
// answerA / answerB: parallel vectors of numeric answers.
// weights, mins, maxs: per-question metadata from the questions table.
static int computeScore(const std::vector<double>& answerA,
                        const std::vector<double>& answerB,
                        const std::vector<double>& weights,
                        const std::vector<double>& mins,
                        const std::vector<double>& maxs)
{
    double dist = 0.0, maxDist = 0.0;
    for (size_t i = 0; i < answerA.size(); ++i) {
        double d   = answerA[i] - answerB[i];
        double rng = maxs[i] - mins[i];
        dist    += weights[i] * d * d;
        maxDist += weights[i] * rng * rng;
    }
    if (maxDist <= 0.0) return 100;
    double raw = 100.0 - std::sqrt(dist) / std::sqrt(maxDist) * 100.0;
    return static_cast<int>(std::clamp(std::round(raw), 0.0, 100.0));
}

// Read a file from disk into a string (used to serve static HTML/CSS/JS).
static std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── Algorithm runners (return crow::json::wvalue) ─────────────────────────────

static crow::json::wvalue runGaleShapley()
{
    // Stable matching: men propose in descending-score order;
    // women tentatively accept or upgrade their current partner.
    // Guarantee: no two unmatched people mutually prefer each other.
    GaleShapley gs(buildMenPrefs(), buildWomenPrefs());
    gs.run();

    crow::json::wvalue result = matchesToJson(gs.getMatchingA());
    result["algorithm"]   = "Gale-Shapley";
    result["description"] = "Stable matching — no couple would both prefer each other over their assigned partners.";
    result["stable"]      = gs.isStable();
    result["guarantee"]   = "Proposer-optimal stable matching";
    return result;
}

static crow::json::wvalue runHopcroftKarp()
{
    // Maximum bipartite matching: only edges with score >= THRESHOLD exist.
    // Guarantee: maximum number of compatible couples.
    HopcroftKarp hk(N, N);
    for (int m = 0; m < N; ++m)
        for (int w = 0; w < N; ++w)
            if (COMPAT[m][w] >= THRESHOLD)
                hk.addCompatiblePair(m, w);

    hk.maxMatching();

    crow::json::wvalue result = matchesToJson(hk.getMatching());
    result["algorithm"]   = "Hopcroft-Karp";
    result["description"] = "Maximum bipartite matching — maximises the number of couples from compatible pairs (score >= threshold).";
    result["threshold"]   = THRESHOLD;
    result["guarantee"]   = "Maximum cardinality matching on the compatible-pair graph";
    return result;
}

static crow::json::wvalue runHungarian()
{
    // Optimal assignment: every man is assigned exactly one woman.
    // Guarantee: globally maximum total compatibility score.
    Hungarian hung(COMPAT);
    hung.solve();

    crow::json::wvalue result = matchesToJson(hung.getAssignment());
    result["algorithm"]   = "Hungarian";
    result["description"] = "Optimal assignment — every participant is matched and total compatibility score is maximised.";
    result["maxScore"]    = hung.getMaxScore();
    result["guarantee"]   = "Globally optimal total compatibility score (O(n³))";
    return result;
}

static crow::json::wvalue runBlossom()
{
    // General-graph max matching using Edmonds' Blossom algorithm.
    // Treats the pool as an undirected graph; handles odd cycles that
    // bipartite algorithms cannot.  Same threshold edges as Hopcroft-Karp.
    Blossom bl(2 * N);
    for (int m = 0; m < N; ++m)
        for (int w = 0; w < N; ++w)
            if (COMPAT[m][w] >= THRESHOLD)
                bl.addCompatiblePair(m, N + w);

    bl.maxMatching();
    const auto& raw = bl.getMatching();

    std::vector<int> matchM(N, -1);
    for (int m = 0; m < N; ++m)
        if (raw[m] != -1)
            matchM[m] = raw[m] - N;  // translate vertex index back to woman index

    crow::json::wvalue result = matchesToJson(matchM);
    result["algorithm"]   = "Blossom";
    result["description"] = "General-graph matching (Edmonds' Blossom) — handles odd cycles; same threshold edges as Hopcroft-Karp.";
    result["threshold"]   = THRESHOLD;
    result["guarantee"]   = "Maximum cardinality matching on a general (non-bipartite) graph";
    return result;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    // ── PostgreSQL connection ────────────────────────────────────────────────
    std::string connStr =
        "host="     + getenv_or("DB_HOST",     "db")   +
        " port="    + getenv_or("DB_PORT",     "5432") +
        " dbname="  + getenv_or("DB_NAME",     "")     +
        " user="    + getenv_or("DB_USER",     "")     +
        " password="+ getenv_or("DB_PASSWORD", "")     +
        " sslmode=" + getenv_or("DB_SSLMODE",  "prefer");

    PGconn* db = PQconnectdb(connStr.c_str());
    if (PQstatus(db) != CONNECTION_OK) {
        std::cerr << "WARNING: DB connect failed: " << PQerrorMessage(db) << "\n";
        std::cerr << "The server will start, but database-dependent endpoints will return errors.\n";
    } else {
        std::cout << "Connected to PostgreSQL\n";
    }

    // Serialises all DB access — libpq connections are not thread-safe.
    std::mutex db_mutex;

    crow::SimpleApp app;

    // ── Serve static UI ──────────────────────────────────────────────────────

    // GET /
    // Returns the main single-page application (index.html).
    CROW_ROUTE(app, "/")([](){
        std::string html = readFile("server/static/index.html");
        if (html.empty()) {
            return crow::response(404, "index.html not found");
        }
        crow::response res(html);
        res.set_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // GET /static/<filename>
    // Serves any file from server/static/. Supports .css, .js, .png, .svg.
    CROW_ROUTE(app, "/static/<string>")([](const std::string& filename){
        // TODO: sanitise filename to prevent directory traversal attacks
        //       (strip leading '../', reject absolute paths, etc.)
        static const std::unordered_map<std::string,std::string> mime = {
            {".html","text/html"}, {".css","text/css"}, {".js","application/javascript"},
            {".png","image/png"},  {".svg","image/svg+xml"}, {".ico","image/x-icon"},
        };
        std::string path = "server/static/" + filename;
        std::string content = readFile(path);
        if (content.empty()) return crow::response(404);

        std::string ext = filename.substr(filename.rfind('.'));
        auto it = mime.find(ext);
        std::string ct = (it != mime.end()) ? it->second : "application/octet-stream";

        crow::response res(content);
        res.set_header("Content-Type", ct);
        return res;
    });

    // ── User Management ───────────────────────────────────────────────────────

    // POST /api/users/register
    // Body: { "alias", "real_name", "email", "password", "gender", "age", ["bio"], ["interests"] }
    // Hashes the password via pgcrypto's crypt()/gen_salt('bf') in SQL.
    // Returns: { "id": int, "alias": string }
    CROW_ROUTE(app, "/api/users/register").methods(crow::HTTPMethod::POST)(
    [&db, &db_mutex](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "{\"error\":\"invalid JSON\"}");

        // Validate required fields
        for (const char* f : {"alias","real_name","email","password","gender","age"}) {
            if (!body.has(f)) {
                crow::json::wvalue e; e["error"] = std::string("missing field: ") + f;
                return crow::response(400, e);
            }
        }

        std::string alias     = body["alias"].s();
        std::string real_name = body["real_name"].s();
        std::string email     = body["email"].s();
        std::string password  = body["password"].s();
        std::string gender    = body["gender"].s();
        std::string age_str   = std::to_string(body["age"].i());
        std::string bio       = body.has("bio") ? std::string(body["bio"].s()) : "";

        if (gender != "male" && gender != "female") {
            crow::json::wvalue e; e["error"] = "gender must be 'male' or 'female'";
            return crow::response(400, e);
        }

        int age_val = static_cast<int>(body["age"].i());
        if (age_val < 18 || age_val > 120) {
            crow::json::wvalue e; e["error"] = "age must be between 18 and 120";
            return crow::response(400, e);
        }

        // Build interests as a PostgreSQL array literal e.g. "{hiking,jazz}"
        std::string interests_arr = "{}";
        if (body.has("interests") && body["interests"].t() == crow::json::type::List) {
            interests_arr = "{";
            bool first = true;
            for (const auto& item : body["interests"]) {
                if (!first) interests_arr += ",";
                interests_arr += "\"" + std::string(item.s()) + "\"";
                first = false;
            }
            interests_arr += "}";
        }

        const char* params[] = {
            alias.c_str(), real_name.c_str(), email.c_str(), password.c_str(),
            gender.c_str(), age_str.c_str(), bio.c_str(), interests_arr.c_str()
        };

        std::lock_guard<std::mutex> lock(db_mutex);
        PGresult* pgres = PQexecParams(db,
            "INSERT INTO users (alias, real_name, email, password_hash, gender, age, bio, interests) "
            "VALUES ($1, $2, $3, crypt($4, gen_salt('bf')), $5::gender_type, $6::smallint, "
            "NULLIF($7,''), $8::text[]) "
            "RETURNING id, alias",
            8, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(pgres) != PGRES_TUPLES_OK) {
            std::string sqlstate = PQresultErrorField(pgres, PG_DIAG_SQLSTATE);
            PQclear(pgres);
            if (sqlstate == "23505") {
                crow::json::wvalue e; e["error"] = "email already registered";
                return crow::response(409, e);
            }
            crow::json::wvalue e; e["error"] = "registration failed";
            return crow::response(500, e);
        }

        crow::json::wvalue out;
        out["id"]    = std::stoll(PQgetvalue(pgres, 0, 0));
        out["alias"] = std::string(PQgetvalue(pgres, 0, 1));
        PQclear(pgres);
        return crow::response(201, out);
    });

    // POST /api/users/login
    // Body: { "email": string, "password": string }
    // Verifies credentials via pgcrypto's crypt() comparison.
    // Returns: { "userId": int, "alias": string, "role": string }
    CROW_ROUTE(app, "/api/users/login").methods(crow::HTTPMethod::POST)(
    [&db, &db_mutex](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "{\"error\":\"invalid JSON\"}");

        if (!body.has("email") || !body.has("password")) {
            crow::json::wvalue e; e["error"] = "email and password required";
            return crow::response(400, e);
        }

        std::string email    = body["email"].s();
        std::string password = body["password"].s();

        const char* params[] = { email.c_str(), password.c_str() };

        std::lock_guard<std::mutex> lock(db_mutex);
        PGresult* pgres = PQexecParams(db,
            "SELECT id, alias, role FROM users "
            "WHERE email = $1 "
            "  AND password_hash = crypt($2, password_hash) "
            "  AND deleted_at IS NULL",
            2, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(pgres) != PGRES_TUPLES_OK || PQntuples(pgres) == 0) {
            PQclear(pgres);
            crow::json::wvalue e; e["error"] = "invalid email or password";
            return crow::response(401, e);
        }

        crow::json::wvalue out;
        out["userId"] = std::stoll(PQgetvalue(pgres, 0, 0));
        out["alias"]  = std::string(PQgetvalue(pgres, 0, 1));
        out["role"]   = std::string(PQgetvalue(pgres, 0, 2));
        PQclear(pgres);
        return crow::response(200, out);
    });

    // GET /api/users
    // Returns a paginated list of active participants (no PII).
    // Query params: ?page=1&limit=20&gender=male|female
    // TODO: gate behind admin JWT for production
    CROW_ROUTE(app, "/api/users").methods(crow::HTTPMethod::GET)(
    [&db, &db_mutex](const crow::request& req){
        int page  = 1, limit = 20;
        if (req.url_params.get("page"))  page  = std::max(1, std::stoi(req.url_params.get("page")));
        if (req.url_params.get("limit")) limit = std::max(1, std::min(100, std::stoi(req.url_params.get("limit"))));
        int offset = (page - 1) * limit;

        std::string limit_s  = std::to_string(limit);
        std::string offset_s = std::to_string(offset);

        // Optional gender filter
        const char* gender_param = req.url_params.get("gender");
        std::string gender_s = gender_param ? gender_param : "";

        std::lock_guard<std::mutex> lock(db_mutex);
        PGresult* pgres;

        if (!gender_s.empty() && (gender_s == "male" || gender_s == "female")) {
            const char* params[] = { gender_s.c_str(), limit_s.c_str(), offset_s.c_str() };
            pgres = PQexecParams(db,
                "SELECT id, alias, gender, registered_at, has_completed_questionnaire "
                "FROM users WHERE deleted_at IS NULL AND gender = $1::gender_type "
                "ORDER BY registered_at DESC LIMIT $2::int OFFSET $3::int",
                3, nullptr, params, nullptr, nullptr, 0);
        } else {
            const char* params[] = { limit_s.c_str(), offset_s.c_str() };
            pgres = PQexecParams(db,
                "SELECT id, alias, gender, registered_at, has_completed_questionnaire "
                "FROM users WHERE deleted_at IS NULL "
                "ORDER BY registered_at DESC LIMIT $1::int OFFSET $2::int",
                2, nullptr, params, nullptr, nullptr, 0);
        }

        if (PQresultStatus(pgres) != PGRES_TUPLES_OK) {
            PQclear(pgres);
            crow::json::wvalue e; e["error"] = "query failed";
            return crow::response(500, e);
        }

        crow::json::wvalue::list users;
        int rows = PQntuples(pgres);
        for (int i = 0; i < rows; ++i) {
            crow::json::wvalue u;
            u["id"]                       = std::stoll(PQgetvalue(pgres, i, 0));
            u["alias"]                    = std::string(PQgetvalue(pgres, i, 1));
            u["gender"]                   = std::string(PQgetvalue(pgres, i, 2));
            u["registeredAt"]             = std::string(PQgetvalue(pgres, i, 3));
            u["hasCompletedQuestionnaire"] = (std::string(PQgetvalue(pgres, i, 4)) == "t");
            users.push_back(std::move(u));
        }
        PQclear(pgres);

        crow::json::wvalue out;
        out["users"] = std::move(users);
        out["page"]  = page;
        out["limit"] = limit;
        return crow::response(200, out);
    });

    // GET /api/users/:id
    // Returns the public (anonymised) profile of participant :id.
    // real_name, email, password_hash are never returned.
    CROW_ROUTE(app, "/api/users/<int>")([&db, &db_mutex](int id){
        std::string id_s = std::to_string(id);
        const char* params[] = { id_s.c_str() };

        std::lock_guard<std::mutex> lock(db_mutex);
        PGresult* pgres = PQexecParams(db,
            "SELECT id, alias, gender, age, bio, interests, has_completed_questionnaire "
            "FROM users WHERE id = $1::bigint AND deleted_at IS NULL",
            1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(pgres) != PGRES_TUPLES_OK || PQntuples(pgres) == 0) {
            PQclear(pgres);
            crow::json::wvalue e; e["error"] = "user not found";
            return crow::response(404, e);
        }

        crow::json::wvalue out;
        out["id"]                       = std::stoll(PQgetvalue(pgres, 0, 0));
        out["alias"]                    = std::string(PQgetvalue(pgres, 0, 1));
        out["gender"]                   = std::string(PQgetvalue(pgres, 0, 2));
        out["age"]                      = std::stoi(PQgetvalue(pgres, 0, 3));
        // bio and interests are CONFIDENTIAL — return only if not null (post-accept in future)
        if (!PQgetisnull(pgres, 0, 4))
            out["bio"]       = std::string(PQgetvalue(pgres, 0, 4));
        if (!PQgetisnull(pgres, 0, 5))
            out["interests"] = std::string(PQgetvalue(pgres, 0, 5));
        out["hasCompletedQuestionnaire"] = (std::string(PQgetvalue(pgres, 0, 6)) == "t");
        PQclear(pgres);
        return crow::response(200, out);
    });

    // PUT /api/users/:id
    // Body: any subset of { alias, bio, interests, age }
    // updated_at is auto-touched by the set_updated_at trigger.
    // TODO: verify JWT matches id (or is admin) for production
    CROW_ROUTE(app, "/api/users/<int>").methods(crow::HTTPMethod::PUT)(
    [&db, &db_mutex](const crow::request& req, int id){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "{\"error\":\"invalid JSON\"}");

        // Build a dynamic SET clause from whichever fields are present
        std::vector<std::string> clauses;
        std::vector<std::string> values;

        if (body.has("alias"))     { clauses.push_back("alias = $" + std::to_string(values.size()+1));     values.push_back(body["alias"].s()); }
        if (body.has("bio"))       { clauses.push_back("bio = $" + std::to_string(values.size()+1));       values.push_back(body["bio"].s()); }
        if (body.has("age")) {
            int a = static_cast<int>(body["age"].i());
            if (a < 18 || a > 120) {
                crow::json::wvalue e; e["error"] = "age must be between 18 and 120";
                return crow::response(400, e);
            }
            clauses.push_back("age = $" + std::to_string(values.size()+1) + "::smallint");
            values.push_back(std::to_string(a));
        }
        if (body.has("interests")) {
            std::string arr = "{";
            bool first = true;
            for (const auto& item : body["interests"]) {
                if (!first) arr += ",";
                arr += "\"" + std::string(item.s()) + "\"";
                first = false;
            }
            arr += "}";
            clauses.push_back("interests = $" + std::to_string(values.size()+1) + "::text[]");
            values.push_back(arr);
        }

        if (clauses.empty()) {
            crow::json::wvalue e; e["error"] = "no updatable fields provided";
            return crow::response(400, e);
        }

        // Final param is the id
        std::string id_s = std::to_string(id);
        values.push_back(id_s);
        std::string id_placeholder = "$" + std::to_string(values.size());

        std::string sql = "UPDATE users SET ";
        for (size_t i = 0; i < clauses.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += clauses[i];
        }
        sql += " WHERE id = " + id_placeholder + "::bigint AND deleted_at IS NULL RETURNING id";

        std::vector<const char*> params;
        for (const auto& v : values) params.push_back(v.c_str());

        std::lock_guard<std::mutex> lock(db_mutex);
        PGresult* pgres = PQexecParams(db, sql.c_str(),
            static_cast<int>(params.size()), nullptr, params.data(), nullptr, nullptr, 0);

        if (PQresultStatus(pgres) != PGRES_TUPLES_OK || PQntuples(pgres) == 0) {
            PQclear(pgres);
            crow::json::wvalue e; e["error"] = "user not found or update failed";
            return crow::response(404, e);
        }
        PQclear(pgres);

        crow::json::wvalue out;
        out["id"] = id;
        out["updated"] = true;
        return crow::response(200, out);
    });

    // DELETE /api/users/:id
    // Two-step GDPR erasure: nullify PII columns then soft-delete.
    // Child rows (matches, messages, etc.) are hard-deleted on a later
    // hard-delete via ON DELETE CASCADE in the schema.
    // TODO: verify JWT matches id (or admin) for production
    CROW_ROUTE(app, "/api/users/<int>").methods(crow::HTTPMethod::DELETE)(
    [&db, &db_mutex](const crow::request& /*req*/, int id){
        std::string id_s = std::to_string(id);
        const char* params[] = { id_s.c_str() };

        std::lock_guard<std::mutex> lock(db_mutex);

        // Step 1: nullify PII to satisfy GDPR right-to-erasure
        PGresult* pgres = PQexecParams(db,
            "UPDATE users SET real_name = NULL, email = NULL, password_hash = NULL, "
            "bio = NULL, interests = '{}' "
            "WHERE id = $1::bigint AND deleted_at IS NULL",
            1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(pgres) != PGRES_COMMAND_OK) {
            PQclear(pgres);
            crow::json::wvalue e; e["error"] = "delete failed";
            return crow::response(500, e);
        }
        if (std::string(PQcmdTuples(pgres)) == "0") {
            PQclear(pgres);
            crow::json::wvalue e; e["error"] = "user not found";
            return crow::response(404, e);
        }
        PQclear(pgres);

        // Step 2: soft-delete
        pgres = PQexecParams(db,
            "UPDATE users SET deleted_at = NOW() WHERE id = $1::bigint",
            1, nullptr, params, nullptr, nullptr, 0);

        bool ok = (PQresultStatus(pgres) == PGRES_COMMAND_OK);
        PQclear(pgres);
        return ok ? crow::response(204) : crow::response(500);
    });

    // ── Compatibility Questionnaire ────────────────────────────────────────────

    // GET /api/questions
    // Returns all active questions for the current questionnaire version.
    CROW_ROUTE(app, "/api/questions")([](){
        try {
            pqxx::connection conn(pqConnStr());
            // Read-only snapshot — serializable so the question set is consistent.
            pqxx::read_transaction txn(conn);
            setSessionRole(txn, "scoring_service", 0);
            auto rows = txn.exec(
                "SELECT id, question_text, answer_type, min_value, max_value, "
                "       weight, display_order, question_version "
                "FROM questions WHERE is_active = TRUE ORDER BY display_order");
            txn.commit();

            crow::json::wvalue::list qs;
            int version = 1;
            for (const auto& r : rows) {
                crow::json::wvalue q;
                q["id"]              = r[0].as<long long>();
                q["questionText"]    = r[1].as<std::string>();
                q["answerType"]      = r[2].as<std::string>();
                q["minValue"]        = r[3].as<int>();
                q["maxValue"]        = r[4].as<int>();
                q["weight"]          = r[5].as<double>();
                q["displayOrder"]    = r[6].as<int>();
                version              = r[7].as<int>();
                qs.push_back(std::move(q));
            }
            crow::json::wvalue res;
            res["questions"]       = std::move(qs);
            res["questionVersion"] = version;
            return crow::response(200, res);
        } catch (const std::exception& ex) {
            crow::json::wvalue e; e["error"] = ex.what();
            return crow::response(500, e);
        }
    });

    // POST /api/questionnaire/:id
    // Body: { "answers": [ { "questionId": int, "answer": int/string }, ... ] }
    // Stores answers, computes pair-wise compatibility scores, updates DB.
    // Uses SERIALIZABLE isolation: we read all current submissions, compute scores,
    // then write — serializable ensures no concurrent submission can produce an
    // inconsistent score matrix (the DB will abort and the client can retry).
    CROW_ROUTE(app, "/api/questionnaire/<int>").methods(crow::HTTPMethod::POST)(
    [](const crow::request& req, int userId){
        auto body = crow::json::load(req.body);
        if (!body || !body.has("answers") || body["answers"].t() != crow::json::type::List) {
            crow::json::wvalue e; e["error"] = "body must be {\"answers\":[...]}";
            return crow::response(400, e);
        }

        try {
            pqxx::connection conn(pqConnStr());
            // SERIALIZABLE: we read other users' submissions and write scores in one
            // atomic view. If two users submit concurrently, PostgreSQL serializes them
            // correctly and aborts/retries whichever transaction conflicts.
            pqxx::transaction<pqxx::isolation_level::serializable> txn(conn);
            setSessionRole(txn, "scoring_service", static_cast<long long>(userId));

            // Step 1: load active questions
            auto qrows = txn.exec(
                "SELECT id, answer_type, min_value, max_value, weight, question_version "
                "FROM questions WHERE is_active = TRUE ORDER BY id");

            // Build lookup: questionId -> {type, min, max, weight, version}
            struct QMeta { std::string type; int min_v, max_v; double weight; int version; };
            std::map<long long, QMeta> qmap;
            int qversion = 1;
            for (const auto& r : qrows) {
                QMeta m{ r[1].as<std::string>(), r[2].as<int>(), r[3].as<int>(),
                          r[4].as<double>(), r[5].as<int>() };
                qmap[r[0].as<long long>()] = m;
                qversion = m.version;
            }

            // Step 2: validate submitted answers
            std::map<long long, double> answerMap;
            const auto& answers = body["answers"];
            for (const auto& a : answers) {
                if (!a.has("questionId") || !a.has("answer")) {
                    crow::json::wvalue e; e["error"] = "each answer must have questionId and answer";
                    return crow::response(400, e);
                }
                long long qid = a["questionId"].i();
                auto it = qmap.find(qid);
                if (it == qmap.end()) {
                    crow::json::wvalue e; e["error"] = "unknown questionId: " + std::to_string(qid);
                    return crow::response(400, e);
                }
                double val = 0;
                if (a["answer"].t() == crow::json::type::Number) {
                    val = a["answer"].d();
                } else {
                    crow::json::wvalue e; e["error"] = "answer must be numeric for scale/integer types";
                    return crow::response(400, e);
                }
                const auto& meta = it->second;
                if (val < meta.min_v || val > meta.max_v) {
                    crow::json::wvalue e;
                    e["error"] = "answer out of range for questionId " + std::to_string(qid);
                    return crow::response(400, e);
                }
                answerMap[qid] = val;
            }

            // Build JSONB string for the answers
            std::string answersJson = "[";
            bool first = true;
            for (const auto& [qid, val] : answerMap) {
                if (!first) answersJson += ",";
                answersJson += "{\"questionId\":" + std::to_string(qid) +
                               ",\"answer\":"     + std::to_string(val) + "}";
                first = false;
            }
            answersJson += "]";

            std::string userId_s   = std::to_string(userId);
            std::string qversion_s = std::to_string(qversion);

            // Step 3: mark old submission non-current
            txn.exec_params(
                "UPDATE questionnaire_submissions SET is_current=FALSE "
                "WHERE user_id=$1 AND question_version=$2 AND is_current=TRUE",
                userId, qversion);

            // Step 4: insert new submission
            auto srow = txn.exec_params1(
                "INSERT INTO questionnaire_submissions "
                "(user_id, question_version, answers, is_current) "
                "VALUES ($1,$2,$3::jsonb,TRUE) RETURNING id",
                userId, qversion, answersJson);
            long long submissionId = srow[0].as<long long>();

            // Step 5: mark questionnaire complete
            txn.exec_params(
                "UPDATE users SET has_completed_questionnaire=TRUE WHERE id=$1",
                userId);

            // Step 6: determine submitter gender
            auto grow = txn.exec_params1(
                "SELECT gender FROM users WHERE id=$1 AND deleted_at IS NULL",
                userId);
            std::string myGender = grow[0].as<std::string>();
            std::string otherGender = (myGender == "male") ? "female" : "male";

            // Fetch other-gender users with current submissions
            auto others = txn.exec_params(
                "SELECT u.id, qs.id AS sub_id, qs.answers "
                "FROM users u "
                "JOIN questionnaire_submissions qs "
                "  ON qs.user_id = u.id AND qs.question_version = $1 AND qs.is_current = TRUE "
                "WHERE u.gender = $2::gender_type AND u.deleted_at IS NULL",
                qversion, otherGender);

            // Build sorted question id/weight/min/max vectors for scoring
            std::vector<long long> qids;
            std::vector<double> weights, mins, maxs;
            for (const auto& [qid, meta] : qmap) {
                qids.push_back(qid);
                weights.push_back(meta.weight);
                mins.push_back(meta.min_v);
                maxs.push_back(meta.max_v);
            }

            // My answer vector (ordered by qids)
            std::vector<double> myAnswers;
            for (auto qid : qids) {
                auto it = answerMap.find(qid);
                myAnswers.push_back(it != answerMap.end() ? it->second : 0.0);
            }

            // Step 7: compute and upsert scores for each other user
            int scoresUpdated = 0;
            for (const auto& other : others) {
                long long otherId    = other[0].as<long long>();
                long long otherSubId = other[1].as<long long>();
                std::string otherAnswersJson = other[2].as<std::string>();

                // Parse other user's answers from JSONB
                // Simple extraction: find "questionId":N,"answer":V pairs
                std::map<long long, double> otherAnswerMap;
                {
                    // Parse the JSONB array manually since we don't have a full JSON lib
                    // Format: [{"questionId":1,"answer":7},...]
                    std::string s = otherAnswersJson;
                    size_t pos = 0;
                    while ((pos = s.find("\"questionId\":", pos)) != std::string::npos) {
                        pos += 13; // skip "questionId":
                        size_t end = s.find_first_of(",}", pos);
                        long long qid = std::stoll(s.substr(pos, end - pos));
                        size_t apos = s.find("\"answer\":", end);
                        if (apos == std::string::npos) break;
                        apos += 9;
                        size_t aend = s.find_first_of(",}", apos);
                        double val = std::stod(s.substr(apos, aend - apos));
                        otherAnswerMap[qid] = val;
                        pos = aend;
                    }
                }

                std::vector<double> otherAnswers;
                for (auto qid : qids)
                    otherAnswers.push_back(otherAnswerMap.count(qid) ? otherAnswerMap[qid] : 0.0);

                int score = computeScore(myAnswers, otherAnswers, weights, mins, maxs);

                long long manId, womanId, manSubId, womanSubId;
                if (myGender == "male") {
                    manId = userId; womanId = otherId;
                    manSubId = submissionId; womanSubId = otherSubId;
                } else {
                    manId = otherId; womanId = userId;
                    manSubId = otherSubId; womanSubId = submissionId;
                }

                txn.exec_params(
                    "INSERT INTO compatibility_scores "
                    "(man_id, woman_id, score, computed_at, man_submission_id, woman_submission_id) "
                    "VALUES ($1,$2,$3::smallint,NOW(),$4,$5) "
                    "ON CONFLICT (man_id, woman_id) DO UPDATE SET "
                    "  score=EXCLUDED.score, computed_at=NOW(), "
                    "  man_submission_id=EXCLUDED.man_submission_id, "
                    "  woman_submission_id=EXCLUDED.woman_submission_id",
                    manId, womanId, score, manSubId, womanSubId);
                ++scoresUpdated;
            }

            txn.commit();

            crow::json::wvalue res;
            res["userId"]        = userId;
            res["submissionId"]  = submissionId;
            res["scoresUpdated"] = scoresUpdated;
            res["message"]       = "Questionnaire submitted successfully";
            return crow::response(200, res);

        } catch (const std::exception& ex) {
            crow::json::wvalue e; e["error"] = ex.what();
            return crow::response(500, e);
        }
    });

    // GET /api/compatibility
    // Returns the full compatibility score matrix from the DB (admin / debug only).
    // Falls back to hardcoded data if no scores exist yet.
    // In production this should be gated behind admin auth.
    CROW_ROUTE(app, "/api/compatibility")([](){
        try {
            pqxx::connection conn(pqConnStr());
            // Read-only snapshot of the score matrix; serializable for consistency.
            pqxx::read_transaction txn(conn);
            setSessionRole(txn, "scoring_service", 0);
            auto rows = txn.exec(
                "SELECT m.alias, m.id, w.alias, w.id, cs.score "
                "FROM compatibility_scores cs "
                "JOIN users m ON m.id = cs.man_id "
                "JOIN users w ON w.id = cs.woman_id "
                "ORDER BY m.id, w.id");

            if (rows.empty()) {
                // No live data yet — return hardcoded matrix
                crow::json::wvalue res;
                crow::json::wvalue::list matrix;
                for (int mi = 0; mi < N; ++mi) {
                    crow::json::wvalue row;
                    row["man"] = MEN[mi];
                    crow::json::wvalue::list scores;
                    for (int wi = 0; wi < N; ++wi) {
                        crow::json::wvalue entry;
                        entry["woman"] = WOMEN[wi];
                        entry["score"] = COMPAT[mi][wi];
                        scores.push_back(std::move(entry));
                    }
                    row["scores"] = std::move(scores);
                    matrix.push_back(std::move(row));
                }
                res["matrix"]    = std::move(matrix);
                res["threshold"] = THRESHOLD;
                res["n"]         = N;
                res["source"]    = "hardcoded";
                return crow::response(200, res);
            }

            // Group rows by man
            std::map<long long, std::pair<std::string, std::vector<crow::json::wvalue>>> byMan;
            std::vector<long long> manOrder;
            for (const auto& r : rows) {
                long long manId = r[1].as<long long>();
                if (byMan.find(manId) == byMan.end()) {
                    byMan[manId].first = r[0].as<std::string>();
                    manOrder.push_back(manId);
                }
                crow::json::wvalue entry;
                entry["woman"] = r[2].as<std::string>();
                entry["score"] = r[4].as<int>();
                byMan[manId].second.push_back(std::move(entry));
            }

            crow::json::wvalue::list matrix;
            for (auto manId : manOrder) {
                crow::json::wvalue row;
                row["man"] = byMan[manId].first;
                crow::json::wvalue::list scores;
                for (auto& e : byMan[manId].second)
                    scores.push_back(std::move(e));
                row["scores"] = std::move(scores);
                matrix.push_back(std::move(row));
            }
            crow::json::wvalue res;
            res["matrix"]    = std::move(matrix);
            res["threshold"] = THRESHOLD;
            res["n"]         = static_cast<int>(manOrder.size());
            res["source"]    = "database";
            return crow::response(200, res);
        } catch (const std::exception& ex) {
            crow::json::wvalue e; e["error"] = ex.what();
            return crow::response(500, e);
        }
    });

    // ── Matching Algorithms ───────────────────────────────────────────────────
    // Each endpoint runs the algorithm on the current compatibility matrix and
    // returns the resulting pairings together with per-pair scores and summary
    // statistics.  The UI calls these when the user clicks an algorithm button
    // and displays the returned JSON in a modal popup.

    // POST /api/algorithms/gale-shapley
    // Runs the Gale-Shapley stable matching algorithm.
    // Optional body: { "eventId": int } to scope the run to a specific event.
    // Returns: { algorithm, description, guarantee, stable, pairs[], totalScore,
    //            matchedCount, avgScore }
    CROW_ROUTE(app, "/api/algorithms/gale-shapley").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: if eventId provided, load that event's participant subset
        // TODO: build preference lists from the (possibly filtered) score matrix
        // TODO: persist result as an AlgorithmRun record in the DB
        crow::json::wvalue result = runGaleShapley();
        return crow::response(200, result);
    });

    // POST /api/algorithms/hopcroft-karp
    // Runs Hopcroft-Karp maximum bipartite matching.
    // Optional body: { "eventId": int, "threshold": int }
    // Returns: { algorithm, description, guarantee, threshold, pairs[],
    //            totalScore, matchedCount, avgScore }
    CROW_ROUTE(app, "/api/algorithms/hopcroft-karp").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: accept custom threshold from request body (default: THRESHOLD)
        // TODO: filter participants by eventId if provided
        // TODO: persist result
        crow::json::wvalue result = runHopcroftKarp();
        return crow::response(200, result);
    });

    // POST /api/algorithms/hungarian
    // Runs the Hungarian optimal assignment algorithm.
    // Optional body: { "eventId": int }
    // Returns: { algorithm, description, guarantee, maxScore, pairs[],
    //            totalScore, matchedCount, avgScore }
    CROW_ROUTE(app, "/api/algorithms/hungarian").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: filter participants by eventId if provided
        // TODO: build score matrix for selected subset
        // TODO: persist result
        crow::json::wvalue result = runHungarian();
        return crow::response(200, result);
    });

    // POST /api/algorithms/blossom
    // Runs Edmonds' Blossom general-graph maximum matching.
    // Optional body: { "eventId": int, "threshold": int }
    // Returns: { algorithm, description, guarantee, threshold, pairs[],
    //            totalScore, matchedCount, avgScore }
    CROW_ROUTE(app, "/api/algorithms/blossom").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: accept custom threshold from request body
        // TODO: filter participants by eventId if provided
        // TODO: persist result
        crow::json::wvalue result = runBlossom();
        return crow::response(200, result);
    });

    // GET /api/algorithms/compare
    // Runs all four algorithms and returns their results side-by-side for the
    // comparative analysis table shown in the UI.
    CROW_ROUTE(app, "/api/algorithms/compare")([](){
        crow::json::wvalue res;
        crow::json::wvalue::list results;

        results.push_back(runGaleShapley());
        results.push_back(runHopcroftKarp());
        results.push_back(runHungarian());
        results.push_back(runBlossom());

        res["results"] = std::move(results);
        res["n"]       = N;
        res["men"]     = []{
            crow::json::wvalue::list l;
            for (auto& s : MEN) l.push_back(s);
            return l;
        }();
        res["women"]   = []{
            crow::json::wvalue::list l;
            for (auto& s : WOMEN) l.push_back(s);
            return l;
        }();
        return crow::response(200, res);
    });

    // ── Matches ───────────────────────────────────────────────────────────────

    // GET /api/matches
    // Returns all current matches for the authenticated user.
    // Includes match status: pending | accepted | declined.
    CROW_ROUTE(app, "/api/matches")([](){
        // TODO: extract userId from JWT
        // TODO: query matches table for rows where userId appears on either side
        // TODO: join with users table to add partner alias (not real name yet)
        // TODO: return [ { matchId, partner: {alias, bio}, score, status, createdAt } ]
        crow::json::wvalue res;
        res["matches"] = crow::json::wvalue::list{};
        res["message"] = "matches list — not yet wired to a database";
        return crow::response(200, res);
    });

    // GET /api/matches/:id
    // Full details of match :id — visible to either matched participant.
    CROW_ROUTE(app, "/api/matches/<int>")([](int matchId){
        // TODO: verify requester is one of the two matched users
        // TODO: return full match details including compatibility breakdown
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "match detail — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/matches/:id/accept
    // Marks the authenticated user as interested in match :id.
    // When both parties accept, reveal real names and unlock messaging.
    CROW_ROUTE(app, "/api/matches/<int>/accept").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int matchId){
        // TODO: update match record: set acceptedBy[userId] = true
        // TODO: if both sides accepted, flip status to "accepted"
        //       and send push / email notification to both users
        // TODO: return { matchId, status, mutuallyAccepted: bool }
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "accept match — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/matches/:id/decline
    // Declines the match; removes the pairing and suppresses future re-matching
    // of the same pair for the current event cycle.
    CROW_ROUTE(app, "/api/matches/<int>/decline").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int matchId){
        // TODO: update match status to "declined"
        // TODO: optionally record the declining userId so the algorithm can
        //       exclude this pair in future runs (soft-block)
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "decline match — not yet wired to a database";
        return crow::response(200, res);
    });

    // ── Events (speed-dating rounds) ──────────────────────────────────────────

    // GET /api/events
    // Lists all upcoming and past blind-dating events.
    // Query params: ?status=upcoming|past|all  (default: upcoming)
    CROW_ROUTE(app, "/api/events")([](){
        // TODO: query events table filtered by status
        // TODO: return [ { id, name, date, maxParticipants, registeredCount, status } ]
        crow::json::wvalue res;
        res["events"]  = crow::json::wvalue::list{};
        res["message"] = "events list — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/events
    // Admin only.  Creates a new blind-dating event.
    // Body: { "name": string, "date": ISO8601, "maxParticipants": int,
    //         "algorithmType": "gale-shapley"|"hungarian"|... }
    CROW_ROUTE(app, "/api/events").methods(crow::HTTPMethod::POST)(
    [](const crow::request& req){
        // TODO: verify admin JWT
        // TODO: validate body fields
        // TODO: insert into events table; return created event object
        crow::json::wvalue res;
        res["message"] = "create event — not yet wired to a database";
        return crow::response(501, res);
    });

    // GET /api/events/:id
    // Returns event details and the anonymised participant list.
    CROW_ROUTE(app, "/api/events/<int>")([](int eventId){
        // TODO: fetch event row + participant count
        // TODO: return participant aliases (no real names) for privacy
        crow::json::wvalue res;
        res["eventId"] = eventId;
        res["message"] = "event detail — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/events/:id/register
    // Registers the authenticated user for event :id.
    CROW_ROUTE(app, "/api/events/<int>/register").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int eventId){
        // TODO: check event is not full and not already started
        // TODO: check user has completed the questionnaire
        // TODO: insert event_participants row
        // TODO: return { eventId, userId, registeredAt }
        crow::json::wvalue res;
        res["eventId"] = eventId;
        res["message"] = "event registration — not yet wired to a database";
        return crow::response(200, res);
    });

    // GET /api/events/:id/results
    // Returns the stored algorithm results for a completed event.
    // Includes all four algorithm runs for comparison in the UI.
    CROW_ROUTE(app, "/api/events/<int>/results")([](int eventId){
        // TODO: look up stored algorithm run records for eventId
        // TODO: if no stored results, run all four algorithms on the event's
        //       participant subset and persist before returning
        crow::json::wvalue res;
        res["eventId"] = eventId;
        res["message"] = "event results — not yet wired to a database";
        return crow::response(200, res);
    });

    // ── Messages ─────────────────────────────────────────────────────────────

    // GET /api/messages/:matchId
    // Retrieves the message history for a mutually-accepted match.
    // Query params: ?before=<messageId>&limit=50  (cursor pagination)
    CROW_ROUTE(app, "/api/messages/<int>")([](int matchId){
        // TODO: verify requester is party to the match
        // TODO: verify match status is "accepted" (both sides)
        // TODO: query messages table ordered by sentAt DESC, paginate via cursor
        // TODO: return { messages: [ {id, senderId, text, sentAt} ], hasMore: bool }
        crow::json::wvalue res;
        res["matchId"]  = matchId;
        res["messages"] = crow::json::wvalue::list{};
        res["message"]  = "message history — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/messages/:matchId
    // Sends a message to the matched partner.
    // Body: { "text": string }
    CROW_ROUTE(app, "/api/messages/<int>").methods(crow::HTTPMethod::POST)(
    [](const crow::request& req, int matchId){
        // TODO: verify requester is party to the match
        // TODO: verify match is mutually accepted
        // TODO: validate message text (length, content policy)
        // TODO: insert into messages table; broadcast via WebSocket to recipient
        // TODO: return { id, senderId, text, sentAt }
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "send message — not yet wired to a database";
        return crow::response(200, res);
    });

    // ── Start server ──────────────────────────────────────────────────────────
    const char* port_env = std::getenv("PORT");
    uint16_t port = port_env ? static_cast<uint16_t>(std::stoi(port_env)) : 8081;
    std::cout << "Blind Dating API server listening on 0.0.0.0:" << port << "\n";
    app.port(port).bindaddr("0.0.0.0").multithreaded().run();
    PQfinish(db);
    return 0;
}
