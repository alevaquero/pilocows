-- Pilocows — sessions synced from the handheld device

PRAGMA foreign_keys = ON;

-- ─── Sessions ────────────────────────────────────────────────────────────────
-- One row per handheld session, identified by (handheld_session_id, device_id).
-- Re-syncing the same session upserts (updates) the row.
CREATE TABLE IF NOT EXISTS sessions (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    handheld_session_id INTEGER NOT NULL,
    device_id           TEXT    NOT NULL,   -- BT MAC address of the source handheld
    name                TEXT    NOT NULL DEFAULT '',
    type                INTEGER NOT NULL DEFAULT 0,  -- session_type_t: 0=general 1=weighing 2=vaccination 3=pregnancy 4=tb_test 5=removal
    created_at          TEXT    NOT NULL,             -- ISO-8601, from handheld RTC
    tag_count           INTEGER NOT NULL DEFAULT 0,
    handheld_note       TEXT    NOT NULL DEFAULT '',  -- free-text note entered on the handheld
    farm                TEXT    NOT NULL DEFAULT '',  -- frontend-only metadata
    operator            TEXT    NOT NULL DEFAULT '',  -- frontend-only metadata
    comments            TEXT    NOT NULL DEFAULT '',  -- frontend-only metadata
    synced_at           TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
    UNIQUE(handheld_session_id, device_id)
);

CREATE INDEX IF NOT EXISTS idx_sessions_device ON sessions(device_id);
CREATE INDEX IF NOT EXISTS idx_sessions_created ON sessions(created_at DESC);

-- ─── Session Records ──────────────────────────────────────────────────────────
-- One row per unique EID per session.
-- Re-syncing upserts: the most recent scan for an animal wins.
CREATE TABLE IF NOT EXISTS session_records (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    eid         TEXT    NOT NULL,
    scanned_at  TEXT    NOT NULL,               -- ISO-8601, from handheld RTC
    event_data  TEXT    NOT NULL DEFAULT '{}',  -- JSON blob; content varies by session type
    note        TEXT    NOT NULL DEFAULT '',    -- per-animal note
    UNIQUE(session_id, eid)
);

CREATE INDEX IF NOT EXISTS idx_session_records_session ON session_records(session_id);
CREATE INDEX IF NOT EXISTS idx_session_records_eid     ON session_records(eid);
