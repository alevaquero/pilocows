-- Link health records fanned out from session_records back to their
-- originating session + tag, so a per-tag voice note recorded on the
-- handheld (audio lives in session_records.audio, keyed by session_id+eid)
-- can be surfaced on the Animal Detail page's health tabs too, not just the
-- Session Detail page. Nullable: historical rows and anything created
-- directly via the frontend UI (not from a handheld sync) have no session
-- to link back to.

ALTER TABLE weights      ADD COLUMN session_id INTEGER REFERENCES sessions(id) ON DELETE SET NULL;
ALTER TABLE weights      ADD COLUMN eid        TEXT;

ALTER TABLE pregnancies  ADD COLUMN session_id INTEGER REFERENCES sessions(id) ON DELETE SET NULL;
ALTER TABLE pregnancies  ADD COLUMN eid        TEXT;

ALTER TABLE tests        ADD COLUMN session_id INTEGER REFERENCES sessions(id) ON DELETE SET NULL;
ALTER TABLE tests        ADD COLUMN eid        TEXT;

ALTER TABLE vaccinations ADD COLUMN session_id INTEGER REFERENCES sessions(id) ON DELETE SET NULL;
ALTER TABLE vaccinations ADD COLUMN eid        TEXT;
