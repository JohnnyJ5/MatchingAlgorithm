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
#include "db_manager.h"
#include "thread_pool.h"

#include "../src/gale_shapley.h"
#include "../src/hopcroft_karp.h"
#include "../src/hungarian.h"
#include "../src/blossom.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
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
    int totalScore = 0;
    int matchedCount = 0;

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
    return (v != nullptr) ? v : fallback;
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

// Map a DbErrc to the corresponding HTTP status code.
static int dbErrToHttp(DbErrc code) {
    switch (code) {
        case DbErrc::NotFound:     return 404;
        case DbErrc::Conflict:     return 409;
        case DbErrc::Unauthorized: return 401;
        case DbErrc::InvalidInput: return 400;
        default:                   return 500;
    }
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
    // ── Database connection pool ─────────────────────────────────────────────
    const std::string connStr =
        "host="     + getenv_or("DB_HOST",     "db")   +
        " port="    + getenv_or("DB_PORT",     "5432") +
        " dbname="  + getenv_or("DB_NAME",     "")     +
        " user="    + getenv_or("DB_USER",     "")     +
        " password="+ getenv_or("DB_PASSWORD", "")     +
        " sslmode=" + getenv_or("DB_SSLMODE",  "prefer");

    const int poolSize = []() -> int {
        const char* v = std::getenv("DB_POOL_SIZE");
        return (v != nullptr && std::stoi(v) > 0) ? std::stoi(v) : 4;
    }();

    DBManager db(connStr, poolSize);

    // Background thread pool for async work (algorithm re-runs, notifications).
    ThreadPool<> bgPool(2);

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
    // Returns: { "id": int, "alias": string }
    CROW_ROUTE(app, "/api/users/register").methods(crow::HTTPMethod::POST)(
    [&db](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, R"({"error":"invalid JSON"})");

        for (const char* f : {"alias","real_name","email","password","gender","age"}) {
            if (!body.has(f)) {
                crow::json::wvalue e; e["error"] = std::string("missing field: ") + f;
                return crow::response(400, e);
            }
        }

        const std::string alias     = body["alias"].s();
        const std::string real_name = body["real_name"].s();
        const std::string email     = body["email"].s();
        const std::string password  = body["password"].s();
        const std::string gender    = body["gender"].s();
        const std::string bio       = body.has("bio") ? std::string(body["bio"].s()) : "";

        if (gender != "male" && gender != "female") {
            crow::json::wvalue e; e["error"] = "gender must be 'male' or 'female'";
            return crow::response(400, e);
        }

        const int age_val = static_cast<int>(body["age"].i());
        if (age_val < 18 || age_val > 120) {
            crow::json::wvalue e; e["error"] = "age must be between 18 and 120";
            return crow::response(400, e);
        }

        std::vector<std::string> interests;
        if (body.has("interests") && body["interests"].t() == crow::json::type::List)
            for (const auto& item : body["interests"])
                interests.push_back(std::string(item.s()));

        auto result = db.registerUser(alias, real_name, email, password,
                                      gender, age_val, bio, interests);
        if (!dbOk(result)) {
            const auto& err = dbError(result);
            crow::json::wvalue e;
            e["error"] = (err.code == DbErrc::Conflict)
                         ? "email already registered" : "registration failed";
            return crow::response(dbErrToHttp(err.code), e);
        }

        const auto& u = dbValue(result);
        crow::json::wvalue out;
        out["id"]    = u.id;
        out["alias"] = u.alias;
        return crow::response(201, out);
    });

    // POST /api/users/login
    // Body: { "email": string, "password": string }
    // Returns: { "userId": int, "alias": string, "role": string }
    CROW_ROUTE(app, "/api/users/login").methods(crow::HTTPMethod::POST)(
    [&db](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, R"({"error":"invalid JSON"})");

        if (!body.has("email") || !body.has("password")) {
            crow::json::wvalue e; e["error"] = "email and password required";
            return crow::response(400, e);
        }

        auto result = db.loginUser(std::string(body["email"].s()), std::string(body["password"].s()));
        if (!dbOk(result)) {
            crow::json::wvalue e; e["error"] = "invalid email or password";
            return crow::response(401, e);
        }

        const auto& u = dbValue(result);
        crow::json::wvalue out;
        out["userId"] = u.userId;
        out["alias"]  = u.alias;
        out["role"]   = u.role;
        return crow::response(200, out);
    });

    // GET /api/users
    // Returns a paginated list of active participants (no PII).
    // Query params: ?page=1&limit=20&gender=male|female
    // TODO: gate behind admin JWT for production
    CROW_ROUTE(app, "/api/users").methods(crow::HTTPMethod::GET)(
    [&db](const crow::request& req){
        int page  = 1;
        int limit = 20;
        if (req.url_params.get("page") != nullptr)  page  = std::max(1, std::stoi(req.url_params.get("page")));
        if (req.url_params.get("limit") != nullptr) limit = std::max(1, std::min(100, std::stoi(req.url_params.get("limit"))));

        const char* gp = req.url_params.get("gender");
        const std::string gender_filter = (gp != nullptr) ? gp : "";

        auto result = db.listUsers(page, limit, gender_filter);
        if (!dbOk(result)) {
            crow::json::wvalue e; e["error"] = "query failed";
            return crow::response(500, e);
        }

        const auto& r = dbValue(result);
        crow::json::wvalue::list users;
        for (const auto& u : r.users) {
            crow::json::wvalue row;
            row["id"]                       = u.id;
            row["alias"]                    = u.alias;
            row["gender"]                   = u.gender;
            row["registeredAt"]             = u.registeredAt;
            row["hasCompletedQuestionnaire"] = u.hasCompletedQuestionnaire;
            users.push_back(std::move(row));
        }

        crow::json::wvalue out;
        out["users"] = std::move(users);
        out["page"]  = r.page;
        out["limit"] = r.limit;
        return crow::response(200, out);
    });

    // GET /api/users/:id
    // Returns the public (anonymised) profile of participant :id.
    // real_name, email, password_hash are never returned.
    CROW_ROUTE(app, "/api/users/<int>")([&db](int id){
        auto result = db.getUser(static_cast<int64_t>(id));
        if (!dbOk(result)) {
            crow::json::wvalue e; e["error"] = "user not found";
            return crow::response(404, e);
        }

        const auto& p = dbValue(result);
        crow::json::wvalue out;
        out["id"]                       = p.id;
        out["alias"]                    = p.alias;
        out["gender"]                   = p.gender;
        out["age"]                      = p.age;
        // bio and interests are CONFIDENTIAL — return only if not null
        if (p.bio)       out["bio"]       = *p.bio;
        if (p.interests) out["interests"] = *p.interests;
        out["hasCompletedQuestionnaire"] = p.hasCompletedQuestionnaire;
        return crow::response(200, out);
    });

    // PUT /api/users/:id
    // Body: any subset of { alias, bio, interests, age }
    // updated_at is auto-touched by the set_updated_at trigger.
    // TODO: verify JWT matches id (or is admin) for production
    CROW_ROUTE(app, "/api/users/<int>").methods(crow::HTTPMethod::PUT)(
    [&db](const crow::request& req, int id){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, R"({"error":"invalid JSON"})");

        UpdateFields fields;

        if (body.has("alias"))
            fields.alias = std::string(body["alias"].s());
        if (body.has("bio"))
            fields.bio = std::string(body["bio"].s());
        if (body.has("age")) {
            const int a = static_cast<int>(body["age"].i());
            if (a < 18 || a > 120) {
                crow::json::wvalue e; e["error"] = "age must be between 18 and 120";
                return crow::response(400, e);
            }
            fields.age = a;
        }
        if (body.has("interests") && body["interests"].t() == crow::json::type::List) {
            std::vector<std::string> interests;
            for (const auto& item : body["interests"])
                interests.push_back(std::string(item.s()));
            fields.interests = std::move(interests);
        }

        if (!fields.alias && !fields.bio && !fields.age && !fields.interests) {
            crow::json::wvalue e; e["error"] = "no updatable fields provided";
            return crow::response(400, e);
        }

        auto result = db.updateUser(static_cast<int64_t>(id), fields);
        if (!dbOk(result)) {
            const auto& err = dbError(result);
            crow::json::wvalue e; e["error"] = "user not found or update failed";
            return crow::response(dbErrToHttp(err.code), e);
        }

        crow::json::wvalue out;
        out["id"]      = id;
        out["updated"] = true;
        return crow::response(200, out);
    });

    // DELETE /api/users/:id
    // Two-step GDPR erasure: nullify PII columns then soft-delete.
    // TODO: verify JWT matches id (or admin) for production
    CROW_ROUTE(app, "/api/users/<int>").methods(crow::HTTPMethod::DELETE)(
    [&db](const crow::request& /*req*/, int id){
        auto result = db.deleteUser(static_cast<int64_t>(id));
        if (!dbOk(result)) {
            const auto& err = dbError(result);
            if (err.code == DbErrc::NotFound) {
                crow::json::wvalue e; e["error"] = "user not found";
                return crow::response(404, e);
            }
            return crow::response(500);
        }
        return crow::response(204);
    });

    // ── Compatibility Questionnaire ────────────────────────────────────────────

    // POST /api/questionnaire/:id
    // Body: { "answers": [ { "questionId": int, "answer": int/string }, ... ] }
    // Processes questionnaire responses and updates the compatibility matrix.
    CROW_ROUTE(app, "/api/questionnaire/<int>").methods(crow::HTTPMethod::POST)(
    [&db](const crow::request& req, int userId){
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, R"({"error":"invalid JSON"})");

        if (!body.has("answers") || body["answers"].t() != crow::json::type::List) {
            crow::json::wvalue e; e["error"] = "answers array required";
            return crow::response(400, e);
        }

        const int questionVersion = body.has("questionVersion")
                                    ? static_cast<int>(body["questionVersion"].i()) : 1;

        std::vector<QuestionnaireAnswer> answers;
        for (const auto& item : body["answers"]) {
            if (!item.has("questionId") || !item.has("answer")) continue;
            answers.push_back(QuestionnaireAnswer{
                item["questionId"].i(),
                std::string(item["answer"].s())
            });
        }

        auto result = db.submitQuestionnaire(
            static_cast<int64_t>(userId), questionVersion, answers);

        if (!dbOk(result)) {
            const auto& err = dbError(result);
            crow::json::wvalue e; e["error"] = err.message;
            return crow::response(dbErrToHttp(err.code), e);
        }

        const auto& sub = dbValue(result);
        crow::json::wvalue res;
        res["submissionId"] = sub.submissionId;
        res["userId"]       = sub.userId;
        res["submittedAt"]  = sub.submittedAt;
        return crow::response(200, res);
    });

    // GET /api/compatibility
    // Returns the full n×n compatibility score matrix (admin / debug only).
    // In production this should be gated behind admin auth.
    CROW_ROUTE(app, "/api/compatibility")([](){
        crow::json::wvalue res;
        crow::json::wvalue::list matrix;
        for (int m = 0; m < N; ++m) {
            crow::json::wvalue row;
            row["man"] = MEN[m];
            crow::json::wvalue::list scores;
            for (int w = 0; w < N; ++w) {
                crow::json::wvalue entry;
                entry["woman"] = WOMEN[w];
                entry["score"] = COMPAT[m][w];
                scores.push_back(std::move(entry));
            }
            row["scores"] = std::move(scores);
            matrix.push_back(std::move(row));
        }
        res["matrix"]    = std::move(matrix);
        res["threshold"] = THRESHOLD;
        res["n"]         = N;
        return crow::response(200, res);
    });

    // ── Matching Algorithms ───────────────────────────────────────────────────
    // Each endpoint runs the algorithm on the current compatibility matrix and
    // returns the resulting pairings together with per-pair scores and summary
    // statistics.  The UI calls these when the user clicks an algorithm button
    // and displays the returned JSON in a modal popup.

    // POST /api/algorithms/gale-shapley
    CROW_ROUTE(app, "/api/algorithms/gale-shapley").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: if eventId provided, load that event's participant subset
        // TODO: build preference lists from the (possibly filtered) score matrix
        // TODO: persist result as an AlgorithmRun record in the DB
        crow::json::wvalue result = runGaleShapley();
        return crow::response(200, result);
    });

    // POST /api/algorithms/hopcroft-karp
    CROW_ROUTE(app, "/api/algorithms/hopcroft-karp").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: accept custom threshold from request body (default: THRESHOLD)
        // TODO: filter participants by eventId if provided
        // TODO: persist result
        crow::json::wvalue result = runHopcroftKarp();
        return crow::response(200, result);
    });

    // POST /api/algorithms/hungarian
    CROW_ROUTE(app, "/api/algorithms/hungarian").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: filter participants by eventId if provided
        // TODO: build score matrix for selected subset
        // TODO: persist result
        crow::json::wvalue result = runHungarian();
        return crow::response(200, result);
    });

    // POST /api/algorithms/blossom
    CROW_ROUTE(app, "/api/algorithms/blossom").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: accept custom threshold from request body
        // TODO: filter participants by eventId if provided
        // TODO: persist result
        crow::json::wvalue result = runBlossom();
        return crow::response(200, result);
    });

    // GET /api/algorithms/compare
    // Runs all four algorithms and returns their results side-by-side.
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
            for (const auto& s : MEN) l.emplace_back(s);
            return l;
        }();
        res["women"]   = []{
            crow::json::wvalue::list l;
            for (const auto& s : WOMEN) l.emplace_back(s);
            return l;
        }();
        return crow::response(200, res);
    });

    // ── Matches ───────────────────────────────────────────────────────────────

    // GET /api/matches
    CROW_ROUTE(app, "/api/matches")([](){
        // TODO: extract userId from JWT
        // TODO: call db.listMatchesForUser(userId)
        crow::json::wvalue res;
        res["matches"] = crow::json::wvalue::list{};
        res["message"] = "matches list — not yet wired to a database";
        return crow::response(200, res);
    });

    // GET /api/matches/:id
    CROW_ROUTE(app, "/api/matches/<int>")([](int matchId){
        // TODO: verify requester is one of the two matched users
        // TODO: call db.getMatch(matchId)
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "match detail — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/matches/:id/accept
    CROW_ROUTE(app, "/api/matches/<int>/accept").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int matchId){
        // TODO: extract userId from JWT, then call db.acceptMatch(matchId, userId)
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "accept match — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/matches/:id/decline
    CROW_ROUTE(app, "/api/matches/<int>/decline").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int matchId){
        // TODO: extract userId from JWT, then call db.declineMatch(matchId, userId)
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "decline match — not yet wired to a database";
        return crow::response(200, res);
    });

    // ── Events (speed-dating rounds) ──────────────────────────────────────────

    // GET /api/events
    CROW_ROUTE(app, "/api/events")([](){
        // TODO: call db.listEvents(statusFilter)
        crow::json::wvalue res;
        res["events"]  = crow::json::wvalue::list{};
        res["message"] = "events list — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/events
    CROW_ROUTE(app, "/api/events").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/){
        // TODO: verify admin JWT
        // TODO: validate body, call db.createEvent(input)
        crow::json::wvalue res;
        res["message"] = "create event — not yet wired to a database";
        return crow::response(501, res);
    });

    // GET /api/events/:id
    CROW_ROUTE(app, "/api/events/<int>")([](int eventId){
        // TODO: call db.getEvent(eventId) + db.getEventParticipants(eventId)
        crow::json::wvalue res;
        res["eventId"] = eventId;
        res["message"] = "event detail — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/events/:id/register
    CROW_ROUTE(app, "/api/events/<int>/register").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int eventId){
        // TODO: extract userId from JWT, call db.registerForEvent(eventId, userId)
        crow::json::wvalue res;
        res["eventId"] = eventId;
        res["message"] = "event registration — not yet wired to a database";
        return crow::response(200, res);
    });

    // GET /api/events/:id/results
    CROW_ROUTE(app, "/api/events/<int>/results")([](int eventId){
        // TODO: call db.getEventAlgorithmRuns(eventId); if empty, run algorithms
        //       and call db.persistAlgorithmRun(...) for each result
        crow::json::wvalue res;
        res["eventId"] = eventId;
        res["message"] = "event results — not yet wired to a database";
        return crow::response(200, res);
    });

    // ── Messages ─────────────────────────────────────────────────────────────

    // GET /api/messages/:matchId
    CROW_ROUTE(app, "/api/messages/<int>")([](int matchId){
        // TODO: verify requester is party to the match
        // TODO: call db.getMessages(matchId, limit, beforeId)
        crow::json::wvalue res;
        res["matchId"]  = matchId;
        res["messages"] = crow::json::wvalue::list{};
        res["message"]  = "message history — not yet wired to a database";
        return crow::response(200, res);
    });

    // POST /api/messages/:matchId
    CROW_ROUTE(app, "/api/messages/<int>").methods(crow::HTTPMethod::POST)(
    [](const crow::request& /*req*/, int matchId){
        // TODO: verify requester is party to the match and match is accepted
        // TODO: call db.sendMessage(matchId, senderId, text)
        crow::json::wvalue res;
        res["matchId"] = matchId;
        res["message"] = "send message — not yet wired to a database";
        return crow::response(200, res);
    });

    // ── Start server ──────────────────────────────────────────────────────────
    const char* port_env = std::getenv("PORT");
    uint16_t port = (port_env != nullptr) ? static_cast<uint16_t>(std::stoi(port_env)) : 8081;
    std::cout << "Blind Dating API server listening on 0.0.0.0:" << port << "\n";
    app.port(port).bindaddr("0.0.0.0").multithreaded().run();
    return 0;
}
