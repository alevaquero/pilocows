-- Pilocows backend — initial schema
-- SQLite (sqlx migrations)

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ─── Tags ────────────────────────────────────────────────────────────────────
-- Physical RFID ear tags purchased by the farm.
CREATE TABLE IF NOT EXISTS tags (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    tag_number  TEXT    NOT NULL UNIQUE,   -- 11-digit EID (ISO 11784/85 FDX-B)
    purchased_at TEXT   NOT NULL,          -- ISO-8601 date
    notes       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

-- ─── Animals ─────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS animals (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    tag_id      INTEGER NOT NULL REFERENCES tags(id) ON DELETE RESTRICT,
    name        TEXT    NOT NULL DEFAULT '',
    breed       TEXT    NOT NULL DEFAULT '',
    category    TEXT    NOT NULL DEFAULT '',  -- e.g. cow, heifer, bull, calf
    sex         TEXT    NOT NULL DEFAULT '',  -- M / F
    dob         TEXT,                         -- ISO-8601 date, nullable
    notes       TEXT    NOT NULL DEFAULT '',
    is_active   INTEGER NOT NULL DEFAULT 1,
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
    updated_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_animals_tag_id ON animals(tag_id);

-- ─── Vaccinations ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS vaccinations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    animal_id   INTEGER NOT NULL REFERENCES animals(id) ON DELETE CASCADE,
    vaccine     TEXT    NOT NULL,
    dose        TEXT    NOT NULL DEFAULT '',
    administered_at TEXT NOT NULL,           -- ISO-8601 date
    next_due_at TEXT,                        -- ISO-8601 date, nullable
    notes       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_vaccinations_animal_id ON vaccinations(animal_id);

-- ─── Pregnancies ──────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS pregnancies (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    animal_id   INTEGER NOT NULL REFERENCES animals(id) ON DELETE CASCADE,
    result      TEXT    NOT NULL,            -- pregnant | not_pregnant | unknown
    checked_at  TEXT    NOT NULL,            -- ISO-8601 date
    due_date    TEXT,                        -- ISO-8601 date, nullable
    notes       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_pregnancies_animal_id ON pregnancies(animal_id);

-- ─── TB Tests ─────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS tb_tests (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    animal_id   INTEGER NOT NULL REFERENCES animals(id) ON DELETE CASCADE,
    result      TEXT    NOT NULL,            -- positive | negative | inconclusive
    tested_at   TEXT    NOT NULL,            -- ISO-8601 date
    notes       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_tb_tests_animal_id ON tb_tests(animal_id);

-- ─── Weights ──────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS weights (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    animal_id   INTEGER NOT NULL REFERENCES animals(id) ON DELETE CASCADE,
    weight_kg   REAL    NOT NULL,
    weighed_at  TEXT    NOT NULL,            -- ISO-8601 date
    notes       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_weights_animal_id ON weights(animal_id);

-- ─── Removals ─────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS removals (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    animal_id   INTEGER NOT NULL UNIQUE REFERENCES animals(id) ON DELETE CASCADE,
    reason      TEXT    NOT NULL,            -- sold | died | slaughtered | other
    removed_at  TEXT    NOT NULL,            -- ISO-8601 date
    notes       TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

-- ─── Scan Events ──────────────────────────────────────────────────────────────
-- Raw scan log ingested from handheld via BLE sync.
CREATE TABLE IF NOT EXISTS scan_events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    eid             TEXT    NOT NULL,        -- 11-digit EID as received from handheld
    event_type      TEXT    NOT NULL,        -- weighing | vaccination | pregnancy | tb_test | removal | general
    scanned_at      TEXT    NOT NULL,        -- ISO-8601 datetime from handheld RTC
    weight_kg       REAL,
    pregnancy_result TEXT,
    tb_result       TEXT,
    vaccines        TEXT    NOT NULL DEFAULT '',  -- comma-separated vaccine names
    notes           TEXT    NOT NULL DEFAULT '',
    synced_at       TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
    animal_id       INTEGER REFERENCES animals(id) ON DELETE SET NULL  -- linked after registration
);

CREATE INDEX IF NOT EXISTS idx_scan_events_eid        ON scan_events(eid);
CREATE INDEX IF NOT EXISTS idx_scan_events_scanned_at ON scan_events(scanned_at);
