-- Fix vaccination dedup index: include vaccine name so multiple vaccines given
-- on the same date (from a single handheld session) each get their own row.
-- The old index (animal_id, administered_at) silently dropped all but the first
-- vaccine when fan_out() looped over a comma-separated list.

DROP INDEX IF EXISTS idx_vaccinations_sync;

CREATE UNIQUE INDEX idx_vaccinations_sync
    ON vaccinations(animal_id, vaccine, administered_at);
