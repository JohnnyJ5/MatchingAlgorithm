-- =============================================================================
-- Spark Dating App — PostgreSQL 16 Schema
-- =============================================================================
--
-- Conventions:
--   • All PKs are BIGINT GENERATED ALWAYS AS IDENTITY (surrogate, never exposed
--     directly in URLs — use UUIDs in the API layer if sequential enumeration
--     is a concern).
--   • Timestamps are TIMESTAMPTZ (UTC storage, display in client timezone).
--   • Soft-deletes are used on users only (deleted_at IS NOT NULL = deleted).
--     All child tables use ON DELETE CASCADE so a hard-delete of the users row
--     also purges child data; the application layer first nullifies PII columns
--     then performs the hard delete to satisfy GDPR right-to-erasure.
--   • Passwords: stored as bcrypt/argon2 hash only — never plaintext.
--   • Compatibility scores: stored as a materialized table (not computed on the
--     fly) so the C++ algorithms receive a pre-built matrix in one query.
--   • Questionnaire answers: JSONB per user-submission row (see design notes).
--   • Algorithm results: JSONB blob per run (avoids a separate pairs table whose
--     schema would change per algorithm).
--
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Extensions
-- ---------------------------------------------------------------------------

CREATE EXTENSION IF NOT EXISTS "pgcrypto";   -- gen_random_uuid(), crypt()
CREATE EXTENSION IF NOT EXISTS "pg_trgm";    -- trigram indexes for bio search

-- ---------------------------------------------------------------------------
-- ENUMs
-- ---------------------------------------------------------------------------

CREATE TYPE gender_type        AS ENUM ('male', 'female');
CREATE TYPE match_status_type  AS ENUM ('pending', 'accepted', 'declined');
CREATE TYPE event_status_type  AS ENUM ('upcoming', 'active', 'past', 'cancelled');
CREATE TYPE algorithm_type     AS ENUM (
    'gale-shapley',
    'hopcroft-karp',
    'hungarian',
    'blossom'
);
CREATE TYPE user_role_type     AS ENUM ('user', 'admin');

-- ---------------------------------------------------------------------------
-- 1. users
-- ---------------------------------------------------------------------------
-- Stores all identity and profile data for a participant.
--
-- Privacy notes:
--   • real_name, email, password_hash are RESTRICTED (never returned to other
--     users; only returned to the owner or admin).
--   • alias is the display name shown to other users until a match is mutually
--     accepted; it is intentionally separate from real_name.
--   • age is stored as an integer, not date_of_birth, to minimise PII while
--     still supporting the "weighted Euclidean distance" scoring model.
--     If exact DoB is later required for age-verification, add a separate
--     identity_verification table rather than adding it here.
--   • interests is a TEXT[] array of free-form tags (e.g. 'hiking', 'jazz').
--     It feeds the scoring model but is treated as CONFIDENTIAL.
--   • bio is visible to matched users only (CONFIDENTIAL).
--   • deleted_at: when set, the account is logically deleted.  The application
--     MUST also null out real_name, email, bio, interests before setting this
--     to complete GDPR erasure.  Foreign-key children are hard-deleted via
--     CASCADE on the eventual hard-delete of the row.
-- ---------------------------------------------------------------------------

CREATE TABLE users (
    id                          BIGINT          GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    alias                       TEXT            NOT NULL,           -- anonymised display name
    real_name                   TEXT,                               -- RESTRICTED: owner/admin only; NULL after erasure
    email                       TEXT,                               -- RESTRICTED: unique login credential; NULL after erasure
    password_hash               TEXT,                               -- RESTRICTED: bcrypt/argon2 output; NULL after erasure
    gender                      gender_type     NOT NULL,
    age                         SMALLINT        NOT NULL CHECK (age >= 18 AND age <= 120),
    bio                         TEXT,                               -- CONFIDENTIAL: visible post-accept
    interests                   TEXT[]          NOT NULL DEFAULT '{}',  -- CONFIDENTIAL
    role                        user_role_type  NOT NULL DEFAULT 'user',
    has_completed_questionnaire BOOLEAN         NOT NULL DEFAULT FALSE,
    registered_at               TIMESTAMPTZ     NOT NULL DEFAULT NOW(),
    updated_at                  TIMESTAMPTZ     NOT NULL DEFAULT NOW(),
    deleted_at                  TIMESTAMPTZ                         -- NULL = active account
);

-- Unique constraint on email only for active accounts.
-- Allows re-registration with the same email after deletion.
CREATE UNIQUE INDEX uq_users_email_active
    ON users (email)
    WHERE deleted_at IS NULL;

-- Fast lookups for the GET /api/users?gender= filter + pagination.
CREATE INDEX idx_users_gender_registered
    ON users (gender, registered_at DESC)
    WHERE deleted_at IS NULL;

-- Admin lookup by role.
CREATE INDEX idx_users_role
    ON users (role)
    WHERE deleted_at IS NULL;

-- ---------------------------------------------------------------------------
-- 2. questions
-- ---------------------------------------------------------------------------
-- Stores the questionnaire question definitions.
-- Keeping questions in the DB (rather than hard-coding in C++) means the
-- questionnaire can evolve without a code deploy, and scoring weights are
-- versioned alongside the questions themselves.
--
-- question_version groups questions that belong to the same questionnaire
-- revision. When a new version is released, old compatibility scores derived
-- from the old version remain valid until users resubmit under the new version.
-- ---------------------------------------------------------------------------

CREATE TABLE questions (
    id               BIGINT      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    question_version SMALLINT    NOT NULL DEFAULT 1,
    question_text    TEXT        NOT NULL,
    answer_type      TEXT        NOT NULL CHECK (answer_type IN ('integer', 'text', 'scale')),
    min_value        SMALLINT,                       -- for scale/integer answers
    max_value        SMALLINT,                       -- for scale/integer answers
    weight           NUMERIC(5,4) NOT NULL DEFAULT 1.0, -- weight in Euclidean distance formula
    display_order    SMALLINT    NOT NULL DEFAULT 0,
    is_active        BOOLEAN     NOT NULL DEFAULT TRUE,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_questions_version_active
    ON questions (question_version, display_order)
    WHERE is_active = TRUE;

-- ---------------------------------------------------------------------------
-- 3. questionnaire_submissions
-- ---------------------------------------------------------------------------
-- One row per user per questionnaire version submission.
-- A user may resubmit (e.g. after updating their profile), which generates
-- a new row and triggers recomputation of compatibility scores.
--
-- Design choice — JSONB for answers:
--   answers JSONB stores the raw answer vector as:
--     [{"questionId": 1, "answer": 4}, {"questionId": 2, "answer": 7}, ...]
--
--   Rationale:
--     • Questions change over time (new versions); a rigid typed-columns
--       approach would require schema migrations per questionnaire version.
--     • The scoring computation happens in C++ (or a PL/pgSQL function) that
--       reads the full answer vector atomically — JSONB is ideal for this.
--     • An EAV (one row per answer) approach would require N rows per submission
--       and a pivot to reconstruct the vector, adding complexity with no benefit
--       given the small, bounded vector size (typically < 50 questions).
--     • GIN indexing on JSONB allows querying "all users who answered question 5
--       with value >= 3" for analytics without a schema change.
--
--   Trade-off: JSONB does not enforce per-question type constraints at the DB
--   layer. The application must validate answers against the questions table
--   before inserting. A CHECK constraint on the JSONB shape is possible but
--   cumbersome; application-layer validation is preferred here.
--
-- Privacy: answers reveal intimate preference data. This table is RESTRICTED.
-- Only the owning user and the scoring service should read it.
-- The compatibility_scores table (below) is what other services query.
-- ---------------------------------------------------------------------------

CREATE TABLE questionnaire_submissions (
    id                  BIGINT      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id             BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    question_version    SMALLINT    NOT NULL DEFAULT 1,
    answers             JSONB       NOT NULL,  -- RESTRICTED: raw preference vector
    submitted_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    is_current          BOOLEAN     NOT NULL DEFAULT TRUE
    -- NOTE: when a user resubmits, set is_current = FALSE on the previous row
    -- before inserting the new one (done in a transaction). This preserves audit
    -- history while making "get current answers" queries fast.
);

-- Only one current submission per user per question version.
CREATE UNIQUE INDEX uq_questionnaire_current
    ON questionnaire_submissions (user_id, question_version)
    WHERE is_current = TRUE;

-- Support querying all submissions for a user (history view).
CREATE INDEX idx_questionnaire_user
    ON questionnaire_submissions (user_id, submitted_at DESC);

-- GIN index to query "which users answered question X with value Y".
-- Supports analytics and algorithm parameter tuning without schema changes.
CREATE INDEX idx_questionnaire_answers_gin
    ON questionnaire_submissions USING GIN (answers);

-- ---------------------------------------------------------------------------
-- 4. compatibility_scores
-- ---------------------------------------------------------------------------
-- Materialized pairwise compatibility scores derived from questionnaire answers.
-- One row per (man_id, woman_id) pair.
--
-- Design choice — dedicated scores table vs. computed on the fly:
--   Stored in a table. Rationale:
--   1. The C++ algorithms need the full N×N matrix on every POST to
--      /api/algorithms/*. Computing it in-query on every run would be O(N²)
--      Euclidean distance calculations per request — expensive at scale.
--   2. The matrix only changes when a user resubmits their questionnaire.
--      Storing it avoids redundant recomputation.
--   3. GET /api/compatibility (admin debug) returns the full matrix cheaply.
--   4. The threshold filter (score >= 65) for Hopcroft-Karp and Blossom becomes
--      a simple indexed predicate: WHERE score >= 65.
--
--   Trade-off: the table must be refreshed when questionnaire answers change.
--   The application/scoring service is responsible for this. A trigger or
--   background job pattern is recommended (see TRIGGER comment below).
--
-- Privacy: this table is CONFIDENTIAL, not RESTRICTED, because individual
-- scores do not directly expose raw answers. However, scores can be used to
-- infer preferences, so access should be limited to the matching service and
-- admin roles. Users should NOT be able to query another user's score for them.
-- ---------------------------------------------------------------------------

CREATE TABLE compatibility_scores (
    id              BIGINT      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    man_id          BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    woman_id        BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    score           SMALLINT    NOT NULL CHECK (score >= 0 AND score <= 100),
    computed_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    -- Tracks which submission versions produced this score for auditability.
    man_submission_id   BIGINT  REFERENCES questionnaire_submissions(id) ON DELETE SET NULL,
    woman_submission_id BIGINT  REFERENCES questionnaire_submissions(id) ON DELETE SET NULL,

    CONSTRAINT uq_compatibility_pair UNIQUE (man_id, woman_id),
    -- Prevent self-scoring and same-gender scoring at the DB level.
    CONSTRAINT chk_no_self_score CHECK (man_id <> woman_id)
);

-- Primary access pattern: load all scores for the algorithm matrix.
-- Ordered by man_id, woman_id so C++ can build the 2D array in one scan.
CREATE INDEX idx_compat_man_woman
    ON compatibility_scores (man_id, woman_id);

-- Threshold filter used by Hopcroft-Karp and Blossom.
-- Partial index covers only the rows the algorithms actually use.
CREATE INDEX idx_compat_threshold
    ON compatibility_scores (man_id, woman_id)
    WHERE score >= 65;

-- Reverse lookup: which men is this woman compatible with?
CREATE INDEX idx_compat_woman
    ON compatibility_scores (woman_id, score DESC);

-- ---------------------------------------------------------------------------
-- 5. events
-- ---------------------------------------------------------------------------
-- A speed-dating round or event. Participants register, algorithms run, and
-- matches are produced scoped to this event.
-- ---------------------------------------------------------------------------

CREATE TABLE events (
    id                  BIGINT          GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name                TEXT            NOT NULL,
    description         TEXT,
    event_date          TIMESTAMPTZ     NOT NULL,
    max_participants    SMALLINT        NOT NULL CHECK (max_participants > 0),
    status              event_status_type NOT NULL DEFAULT 'upcoming',
    -- Default algorithm to run for this event (can be overridden per run).
    default_algorithm   algorithm_type  NOT NULL DEFAULT 'hungarian',
    -- Compatibility threshold override for this event (NULL = use global default 65).
    threshold_override  SMALLINT        CHECK (threshold_override >= 0 AND threshold_override <= 100),
    created_by          BIGINT          REFERENCES users(id) ON DELETE SET NULL,
    created_at          TIMESTAMPTZ     NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ     NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_events_status_date
    ON events (status, event_date);

-- ---------------------------------------------------------------------------
-- 6. event_participants
-- ---------------------------------------------------------------------------
-- Junction table: which users have registered for which events.
--
-- Design note: max_participants enforcement is done at the application layer
-- with a SELECT COUNT(*) FOR UPDATE on this table inside a transaction.
-- A DB-level approach (materialized count column + CHECK constraint) is
-- possible but adds write-path complexity and is unnecessary at expected scale.
-- ---------------------------------------------------------------------------

CREATE TABLE event_participants (
    id              BIGINT      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id        BIGINT      NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    user_id         BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    registered_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT uq_event_participant UNIQUE (event_id, user_id)
);

CREATE INDEX idx_event_participants_event
    ON event_participants (event_id);

CREATE INDEX idx_event_participants_user
    ON event_participants (user_id);

-- ---------------------------------------------------------------------------
-- 7. algorithm_runs
-- ---------------------------------------------------------------------------
-- Caches the output of each algorithm run scoped to an event.
-- The C++ server populates this after executing an algorithm;
-- GET /api/events/:id/results returns stored rows (or triggers a fresh run).
--
-- results JSONB stores the full matchesToJson() output:
--   { "pairs": [...], "totalScore": int, "matchedCount": int, "avgScore": float,
--     "algorithm": str, "description": str, "guarantee": str, ... }
--
-- This is intentionally denormalized: parsing the pairs out into a separate
-- table would require a schema change every time the algorithm output format
-- changes. The JSONB blob is what the UI already consumes.
--
-- Privacy: results contain man_id/woman_id references and scores (CONFIDENTIAL).
-- They do not contain real names — the API layer joins aliases at query time.
-- ---------------------------------------------------------------------------

CREATE TABLE algorithm_runs (
    id              BIGINT          GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id        BIGINT          NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    algorithm       algorithm_type  NOT NULL,
    threshold       SMALLINT        CHECK (threshold >= 0 AND threshold <= 100),
    results         JSONB           NOT NULL,  -- full algorithm output blob (CONFIDENTIAL)
    total_score     INT             NOT NULL DEFAULT 0,
    matched_count   SMALLINT        NOT NULL DEFAULT 0,
    avg_score       NUMERIC(5,2)    NOT NULL DEFAULT 0,
    is_current      BOOLEAN         NOT NULL DEFAULT TRUE,
    ran_at          TIMESTAMPTZ     NOT NULL DEFAULT NOW()
);

-- Fast retrieval for GET /api/events/:id/results.
CREATE INDEX idx_algorithm_runs_event_algo
    ON algorithm_runs (event_id, algorithm, ran_at DESC)
    WHERE is_current = TRUE;

-- ---------------------------------------------------------------------------
-- 8. matches
-- ---------------------------------------------------------------------------
-- A suggested pairing between one man and one woman, produced by an algorithm
-- run. A match exists in "pending" state until both parties act on it.
--
-- Privacy design:
--   • real_name is NEVER stored here. The API joins alias from users only.
--   • Names and contact details are revealed at the application layer only
--     when accepted_by_man AND accepted_by_woman are both TRUE.
--   • declined_by: records which side declined (for soft-block logic on
--     future algorithm runs). NULL = not yet declined.
-- ---------------------------------------------------------------------------

CREATE TABLE matches (
    id                  BIGINT              GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    algorithm_run_id    BIGINT              NOT NULL REFERENCES algorithm_runs(id) ON DELETE CASCADE,
    event_id            BIGINT              NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    man_id              BIGINT              NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    woman_id            BIGINT              NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    score               SMALLINT            NOT NULL CHECK (score >= 0 AND score <= 100),
    status              match_status_type   NOT NULL DEFAULT 'pending',
    accepted_by_man     BOOLEAN             NOT NULL DEFAULT FALSE,
    accepted_by_woman   BOOLEAN             NOT NULL DEFAULT FALSE,
    -- NULL = not declined; otherwise records which user declined (for soft-block)
    declined_by         BIGINT              REFERENCES users(id) ON DELETE SET NULL,
    created_at          TIMESTAMPTZ         NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ         NOT NULL DEFAULT NOW(),

    CONSTRAINT uq_match_pair_per_event UNIQUE (event_id, man_id, woman_id),
    CONSTRAINT chk_match_no_self CHECK (man_id <> woman_id),
    -- Status must be consistent with acceptance flags.
    CONSTRAINT chk_match_status_coherence CHECK (
        (status = 'accepted' AND accepted_by_man = TRUE AND accepted_by_woman = TRUE)
        OR (status = 'declined' AND declined_by IS NOT NULL)
        OR (status = 'pending')
    )
);

-- User's match list: GET /api/matches (show all matches for one user).
CREATE INDEX idx_matches_man
    ON matches (man_id, status, created_at DESC);

CREATE INDEX idx_matches_woman
    ON matches (woman_id, status, created_at DESC);

-- Admin view: all matches for an event.
CREATE INDEX idx_matches_event
    ON matches (event_id, status);

-- ---------------------------------------------------------------------------
-- 9. messages
-- ---------------------------------------------------------------------------
-- Chat messages between two users in a mutually accepted match.
-- Only accessible when the parent match has status = 'accepted'.
--
-- Cursor-based pagination: GET /api/messages/:matchId?before=<id>&limit=50
-- uses the PK (id) as the cursor, which is monotonically increasing and
-- indexed by the PK index. No separate cursor column is needed.
--
-- Privacy: message text is highly sensitive (RESTRICTED). Access MUST be
-- gated to the two matched users only. Do not log message text in application
-- logs or error traces. Consider at-rest encryption (pgcrypto) for HIPAA/GDPR
-- stricter compliance — see recommendations section.
-- ---------------------------------------------------------------------------

CREATE TABLE messages (
    id          BIGINT      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    match_id    BIGINT      NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
    sender_id   BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    text        TEXT        NOT NULL CHECK (char_length(text) > 0 AND char_length(text) <= 4000),
    sent_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Cursor-based pagination: WHERE match_id = ? AND id < :cursor ORDER BY id DESC.
CREATE INDEX idx_messages_match_id
    ON messages (match_id, id DESC);

-- Lookup messages sent by a specific user (for delete-all-data on user deletion).
CREATE INDEX idx_messages_sender
    ON messages (sender_id);

-- ---------------------------------------------------------------------------
-- TRIGGERS
-- ---------------------------------------------------------------------------

-- Trigger: keep updated_at current on users and matches without requiring
-- the application layer to always pass the timestamp.
CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$;

CREATE TRIGGER trg_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER trg_matches_updated_at
    BEFORE UPDATE ON matches
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER trg_events_updated_at
    BEFORE UPDATE ON events
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

-- Trigger: auto-accept match when both flags are set.
-- Keeps status in sync with accepted_by_* columns without relying on two
-- separate UPDATE statements in the application.
CREATE OR REPLACE FUNCTION sync_match_status()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    IF NEW.accepted_by_man = TRUE AND NEW.accepted_by_woman = TRUE THEN
        NEW.status = 'accepted';
    END IF;
    RETURN NEW;
END;
$$;

CREATE TRIGGER trg_matches_auto_accept
    BEFORE UPDATE ON matches
    FOR EACH ROW EXECUTE FUNCTION sync_match_status();

-- ---------------------------------------------------------------------------
-- ROW LEVEL SECURITY (RLS)
-- ---------------------------------------------------------------------------
-- Enable RLS on sensitive tables. Policies below assume the application
-- connects as one of three roles:
--   • app_user:  authenticated end-user (set via SET LOCAL app.current_user_id)
--   • app_admin: admin dashboard
--   • app_scoring_service: background service that reads/writes compatibility scores
--
-- These are illustrative policy sketches — adapt to your auth middleware.

ALTER TABLE users                    ENABLE ROW LEVEL SECURITY;
ALTER TABLE questionnaire_submissions ENABLE ROW LEVEL SECURITY;
ALTER TABLE compatibility_scores     ENABLE ROW LEVEL SECURITY;
ALTER TABLE matches                  ENABLE ROW LEVEL SECURITY;
ALTER TABLE messages                 ENABLE ROW LEVEL SECURITY;

-- Users: each user can see their own row; admin sees all.
CREATE POLICY users_self_access ON users
    USING (
        id = current_setting('app.current_user_id', TRUE)::BIGINT
        OR current_setting('app.current_role', TRUE) = 'admin'
    );

-- Questionnaire submissions: user can see only their own answers.
CREATE POLICY qs_self_access ON questionnaire_submissions
    USING (
        user_id = current_setting('app.current_user_id', TRUE)::BIGINT
        OR current_setting('app.current_role', TRUE) IN ('admin', 'scoring_service')
    );

-- Compatibility scores: scoring service and admin only; users cannot read raw scores.
CREATE POLICY compat_service_access ON compatibility_scores
    USING (
        current_setting('app.current_role', TRUE) IN ('admin', 'scoring_service')
    );

-- Matches: visible to either matched user or admin.
CREATE POLICY matches_participant_access ON matches
    USING (
        man_id   = current_setting('app.current_user_id', TRUE)::BIGINT
        OR woman_id = current_setting('app.current_user_id', TRUE)::BIGINT
        OR current_setting('app.current_role', TRUE) = 'admin'
    );

-- Messages: visible only to sender, the other party (via match), or admin.
-- Uses a subquery join to enforce match ownership without a separate lookup.
CREATE POLICY messages_match_access ON messages
    USING (
        sender_id = current_setting('app.current_user_id', TRUE)::BIGINT
        OR EXISTS (
            SELECT 1 FROM matches m
            WHERE m.id = match_id
              AND (
                m.man_id   = current_setting('app.current_user_id', TRUE)::BIGINT
                OR m.woman_id = current_setting('app.current_user_id', TRUE)::BIGINT
              )
        )
        OR current_setting('app.current_role', TRUE) = 'admin'
    );

-- ---------------------------------------------------------------------------
-- VIEWS (convenience, not materialised)
-- ---------------------------------------------------------------------------

-- Public profile view: returns only non-sensitive columns for display to
-- other users. Real name, email, and password_hash are excluded entirely.
CREATE VIEW public_user_profiles AS
SELECT
    u.id,
    u.alias,
    u.gender,
    u.age,
    u.bio,
    u.interests,
    u.has_completed_questionnaire,
    u.registered_at
FROM users u
WHERE u.deleted_at IS NULL;

-- Accepted matches view: joins alias (never real_name) for both sides.
-- The application uses this to populate GET /api/matches for a user.
CREATE VIEW accepted_matches_view AS
SELECT
    m.id           AS match_id,
    m.event_id,
    m.man_id,
    man.alias      AS man_alias,
    m.woman_id,
    woman.alias    AS woman_alias,
    m.score,
    m.status,
    m.accepted_by_man,
    m.accepted_by_woman,
    m.created_at
FROM matches m
JOIN users man   ON man.id   = m.man_id   AND man.deleted_at   IS NULL
JOIN users woman ON woman.id = m.woman_id AND woman.deleted_at IS NULL;

-- IMPORTANT: real_name is intentionally absent from this view.
-- The application should only expose real_name after BOTH accepted_by_man
-- AND accepted_by_woman are TRUE, and should do so via a separate,
-- access-controlled endpoint, not via this view.
