-- Pilocows — voice notes (session-level and per-tag), synced from the handheld.
-- Stored as BLOB directly in the row: matches how this backend already works
-- (one self-contained .db file, no existing file-serving infra), and clips
-- are capped at ~320KB (10s/16kHz/16-bit mono) so this stays well within
-- SQLite's comfort zone for BLOB columns.

ALTER TABLE sessions ADD COLUMN note_audio BLOB NULL;
ALTER TABLE session_records ADD COLUMN audio BLOB NULL;
