#include "db/Migrations.hpp"

#include "common/Clock.hpp"
#include "common/Log.hpp"

namespace eden {

namespace {

const char* kSchemaV1 = R"SQL(
CREATE TABLE users (
    user_id       TEXT PRIMARY KEY,
    email         TEXT NOT NULL DEFAULT '',
    display_name  TEXT NOT NULL,
    theme_prefs   TEXT NOT NULL DEFAULT '{}',
    created_at    INTEGER NOT NULL
);

CREATE TABLE documents (
    doc_id          TEXT PRIMARY KEY,
    title           TEXT NOT NULL,
    file_type       TEXT NOT NULL,
    owner_id        TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    is_enhanced     INTEGER NOT NULL DEFAULT 0,
    file_path       TEXT NOT NULL,
    content_hash    TEXT NOT NULL,
    page_count      INTEGER NOT NULL DEFAULT 0,
    char_count      INTEGER NOT NULL DEFAULT 0,
    last_opened_at  INTEGER NOT NULL DEFAULT 0,
    last_position   INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL,
    UNIQUE (owner_id, content_hash)
);
CREATE INDEX idx_documents_owner ON documents(owner_id, last_opened_at DESC);

CREATE TABLE research_packs (
    pack_id       TEXT PRIMARY KEY,
    doc_id        TEXT NOT NULL REFERENCES documents(doc_id) ON DELETE CASCADE,
    start_offset  INTEGER NOT NULL,
    end_offset    INTEGER NOT NULL,
    text_content  TEXT NOT NULL,
    order_index   INTEGER NOT NULL,
    page_index    INTEGER NOT NULL DEFAULT 0,
    origin        TEXT NOT NULL DEFAULT 'manual',
    created_at    INTEGER NOT NULL,
    CHECK (end_offset >= start_offset)
);
CREATE INDEX idx_packs_doc_order ON research_packs(doc_id, order_index);
CREATE INDEX idx_packs_doc_offset ON research_packs(doc_id, start_offset);

CREATE VIRTUAL TABLE pack_fts USING fts5(
    text_content,
    content = 'research_packs',
    content_rowid = 'rowid',
    tokenize = 'unicode61 remove_diacritics 2'
);

CREATE TRIGGER packs_fts_insert AFTER INSERT ON research_packs BEGIN
    INSERT INTO pack_fts(rowid, text_content) VALUES (new.rowid, new.text_content);
END;
CREATE TRIGGER packs_fts_delete AFTER DELETE ON research_packs BEGIN
    INSERT INTO pack_fts(pack_fts, rowid, text_content) VALUES ('delete', old.rowid, old.text_content);
END;
CREATE TRIGGER packs_fts_update AFTER UPDATE OF text_content ON research_packs BEGIN
    INSERT INTO pack_fts(pack_fts, rowid, text_content) VALUES ('delete', old.rowid, old.text_content);
    INSERT INTO pack_fts(rowid, text_content) VALUES (new.rowid, new.text_content);
END;

CREATE TABLE research_cards (
    card_id          TEXT PRIMARY KEY,
    pack_id          TEXT NOT NULL UNIQUE REFERENCES research_packs(pack_id) ON DELETE CASCADE,
    summary_text     TEXT NOT NULL DEFAULT '',
    evidence_json    TEXT NOT NULL DEFAULT '[]',
    ai_images_json   TEXT NOT NULL DEFAULT '[]',
    sources_json     TEXT NOT NULL DEFAULT '[]',
    revision         INTEGER NOT NULL DEFAULT 1,
    created_at       INTEGER NOT NULL,
    updated_at       INTEGER NOT NULL
);

CREATE TABLE card_sections (
    card_id      TEXT NOT NULL REFERENCES research_cards(card_id) ON DELETE CASCADE,
    function_id  TEXT NOT NULL,
    content      TEXT NOT NULL,
    provider     TEXT NOT NULL DEFAULT '',
    created_at   INTEGER NOT NULL,
    PRIMARY KEY (card_id, function_id)
);

CREATE TABLE card_revisions (
    revision_id    INTEGER PRIMARY KEY AUTOINCREMENT,
    card_id        TEXT NOT NULL REFERENCES research_cards(card_id) ON DELETE CASCADE,
    revision       INTEGER NOT NULL,
    snapshot_json  TEXT NOT NULL,
    created_at     INTEGER NOT NULL
);
CREATE INDEX idx_card_revisions_card ON card_revisions(card_id, revision DESC);

CREATE TABLE comments (
    comment_id          TEXT PRIMARY KEY,
    card_id             TEXT NOT NULL REFERENCES research_cards(card_id) ON DELETE CASCADE,
    user_id             TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    content_type        TEXT NOT NULL DEFAULT 'text',
    media_uri           TEXT NOT NULL DEFAULT '',
    body_text           TEXT NOT NULL DEFAULT '',
    is_writer_original  INTEGER NOT NULL DEFAULT 0,
    is_private          INTEGER NOT NULL DEFAULT 1,
    anchor_offset       INTEGER NOT NULL DEFAULT -1,
    media_timestamp_ms  INTEGER NOT NULL DEFAULT 0,
    parent_comment_id   TEXT REFERENCES comments(comment_id) ON DELETE CASCADE,
    created_at          INTEGER NOT NULL,
    edited_at           INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_comments_card ON comments(card_id, is_writer_original DESC, created_at);
CREATE INDEX idx_comments_parent ON comments(parent_comment_id);

CREATE TABLE comment_reactions (
    comment_id  TEXT NOT NULL REFERENCES comments(comment_id) ON DELETE CASCADE,
    user_id     TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    reaction    TEXT NOT NULL,
    created_at  INTEGER NOT NULL,
    PRIMARY KEY (comment_id, user_id, reaction)
);

CREATE TABLE ai_sessions (
    session_id  TEXT PRIMARY KEY,
    user_id     TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    pack_id     TEXT REFERENCES research_packs(pack_id) ON DELETE SET NULL,
    provider    TEXT NOT NULL,
    model       TEXT NOT NULL DEFAULT '',
    function_id TEXT NOT NULL DEFAULT '',
    prompt      TEXT NOT NULL,
    response    TEXT NOT NULL DEFAULT '',
    input_tokens   INTEGER NOT NULL DEFAULT 0,
    output_tokens  INTEGER NOT NULL DEFAULT 0,
    latency_ms     INTEGER NOT NULL DEFAULT 0,
    status      TEXT NOT NULL DEFAULT 'ok',
    created_at  INTEGER NOT NULL
);
CREATE INDEX idx_ai_sessions_pack ON ai_sessions(pack_id, created_at);
CREATE INDEX idx_ai_sessions_user ON ai_sessions(user_id, created_at DESC);

CREATE TABLE collections (
    collection_id  TEXT PRIMARY KEY,
    owner_id       TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    name           TEXT NOT NULL,
    description    TEXT NOT NULL DEFAULT '',
    is_public      INTEGER NOT NULL DEFAULT 0,
    created_at     INTEGER NOT NULL,
    updated_at     INTEGER NOT NULL
);

CREATE TABLE collection_items (
    collection_id  TEXT NOT NULL REFERENCES collections(collection_id) ON DELETE CASCADE,
    pack_id        TEXT NOT NULL REFERENCES research_packs(pack_id) ON DELETE CASCADE,
    position       INTEGER NOT NULL,
    added_at       INTEGER NOT NULL,
    PRIMARY KEY (collection_id, pack_id)
);
CREATE INDEX idx_collection_items_order ON collection_items(collection_id, position);

CREATE TABLE sources (
    source_id   TEXT PRIMARY KEY,
    card_id     TEXT NOT NULL REFERENCES research_cards(card_id) ON DELETE CASCADE,
    url         TEXT NOT NULL,
    title       TEXT NOT NULL DEFAULT '',
    publisher   TEXT NOT NULL DEFAULT '',
    snippet     TEXT NOT NULL DEFAULT '',
    trust       TEXT NOT NULL DEFAULT 'unrated',
    approved    INTEGER NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL
);
CREATE INDEX idx_sources_card ON sources(card_id);

CREATE TABLE source_rules (
    rule_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    domain      TEXT NOT NULL UNIQUE,
    kind        TEXT NOT NULL,
    created_at  INTEGER NOT NULL
);

CREATE TABLE settings (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL,
    updated_at  INTEGER NOT NULL
);
)SQL";

const char* kSchemaV2 = R"SQL(
CREATE TABLE bookmarks (
    bookmark_id  TEXT PRIMARY KEY,
    doc_id       TEXT NOT NULL REFERENCES documents(doc_id) ON DELETE CASCADE,
    char_offset  INTEGER NOT NULL,
    label        TEXT NOT NULL DEFAULT '',
    created_at   INTEGER NOT NULL
);
CREATE INDEX idx_bookmarks_doc ON bookmarks(doc_id, char_offset);

CREATE TABLE reading_sessions (
    session_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    doc_id       TEXT NOT NULL REFERENCES documents(doc_id) ON DELETE CASCADE,
    started_at   INTEGER NOT NULL,
    ended_at     INTEGER NOT NULL DEFAULT 0,
    start_offset INTEGER NOT NULL DEFAULT 0,
    end_offset   INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_reading_sessions_doc ON reading_sessions(doc_id, started_at DESC);
)SQL";

const char* kSchemaV3 = R"SQL(
CREATE TABLE document_texts (
    doc_id         TEXT PRIMARY KEY REFERENCES documents(doc_id) ON DELETE CASCADE,
    text           TEXT NOT NULL,
    pages_json     TEXT NOT NULL DEFAULT '[]',
    chapters_json  TEXT NOT NULL DEFAULT '[]',
    author         TEXT NOT NULL DEFAULT '',
    language       TEXT NOT NULL DEFAULT '',
    warnings_json  TEXT NOT NULL DEFAULT '[]',
    extracted_at   INTEGER NOT NULL
);
)SQL";

const char* kSchemaV4 = R"SQL(
ALTER TABLE comments ADD COLUMN hidden INTEGER NOT NULL DEFAULT 0;
CREATE INDEX idx_comments_user ON comments(user_id, created_at DESC);
)SQL";

}  // namespace

const std::vector<Migration>& allMigrations() {
    static const std::vector<Migration> migrations = {
        {1, "core schema", kSchemaV1},
        {2, "bookmarks and reading sessions", kSchemaV2},
        {3, "extracted document text", kSchemaV3},
        {4, "comment moderation", kSchemaV4},
    };
    return migrations;
}

int latestSchemaVersion() { return allMigrations().back().version; }

int migrate(Database& db) {
    auto guard = db.lock();
    const int current = static_cast<int>(db.userVersion());
    if (current > latestSchemaVersion()) {
        throw DbError(1, "database schema version " + std::to_string(current) +
                             " is newer than this build supports");
    }
    int applied = 0;
    for (const Migration& migration : allMigrations()) {
        if (migration.version <= current) continue;
        logf(LogLevel::Info, "db", "applying migration ", migration.version, ": ", migration.name);
        Transaction transaction(db);
        db.exec(migration.sql);
        db.setUserVersion(migration.version);
        transaction.commit();
        ++applied;
    }
    return applied;
}

}  // namespace eden
