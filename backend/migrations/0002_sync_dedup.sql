-- Unique indices that make BLE sync idempotent.
-- A re-sync of the same handheld session will INSERT OR IGNORE and produce no
-- duplicates, because each scan is identified by (eid, scanned_at) and each
-- derived health record by (animal_id, <date field>).

-- Remove any duplicate scan_events rows that accumulated before this index
-- existed (keep the earliest row of each duplicate group).
DELETE FROM scan_events
WHERE id NOT IN (
    SELECT MIN(id) FROM scan_events GROUP BY eid, scanned_at
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_events_dedup
    ON scan_events(eid, scanned_at);

-- Health tables are populated by fan_out() going forward; they should be empty
-- at this point, but guard with the same dedup pattern just in case.
DELETE FROM pregnancies
WHERE id NOT IN (
    SELECT MIN(id) FROM pregnancies GROUP BY animal_id, checked_at
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_pregnancies_sync
    ON pregnancies(animal_id, checked_at);

DELETE FROM weights
WHERE id NOT IN (
    SELECT MIN(id) FROM weights GROUP BY animal_id, weighed_at
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_weights_sync
    ON weights(animal_id, weighed_at);

DELETE FROM tb_tests
WHERE id NOT IN (
    SELECT MIN(id) FROM tb_tests GROUP BY animal_id, tested_at
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_tb_tests_sync
    ON tb_tests(animal_id, tested_at);

DELETE FROM vaccinations
WHERE id NOT IN (
    SELECT MIN(id) FROM vaccinations GROUP BY animal_id, administered_at
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_vaccinations_sync
    ON vaccinations(animal_id, administered_at);
