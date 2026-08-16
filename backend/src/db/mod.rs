use serde_json::Value;
use sqlx::{sqlite::SqlitePoolOptions, FromRow, SqlitePool};

use crate::models::ScanEvent;

pub async fn connect(database_url: &str) -> sqlx::Result<SqlitePool> {
    let pool = SqlitePoolOptions::new()
        .max_connections(5)
        .connect(database_url)
        .await?;
    Ok(pool)
}

/// Link any scan_events that arrived before the animal was registered, then
/// fan them out into the typed health tables (weights, vaccinations, etc.).
/// Called from create_animal so that pre-existing scans aren't lost.
pub async fn backfill_scan_events(
    pool: &SqlitePool,
    animal_id: i64,
    tag_number: &str,
) -> sqlx::Result<()> {
    // 1. Claim all orphaned scan_events for this tag
    sqlx::query("UPDATE scan_events SET animal_id = ? WHERE eid = ? AND animal_id IS NULL")
        .bind(animal_id)
        .bind(tag_number)
        .execute(pool)
        .await?;

    // 2. Fetch them so we can fan out into health tables
    let scans = sqlx::query_as::<_, ScanEvent>(
        "SELECT * FROM scan_events WHERE eid = ? AND animal_id = ?",
    )
    .bind(tag_number)
    .bind(animal_id)
    .fetch_all(pool)
    .await?;

    // 3. Fan out — mirrors the logic in routes/sync.rs fan_out()
    for scan in &scans {
        match scan.event_type.as_str() {
            "weighing" => {
                if let Some(w) = scan.weight_kg {
                    if w > 0.0 {
                        sqlx::query(
                            "INSERT OR IGNORE INTO weights
                             (animal_id, weight_kg, weighed_at, notes)
                             VALUES (?, ?, ?, ?)",
                        )
                        .bind(animal_id)
                        .bind(w)
                        .bind(&scan.scanned_at)
                        .bind(&scan.notes)
                        .execute(pool)
                        .await?;
                    }
                }
            }
            "pregnancy" => {
                if let Some(ref result) = scan.pregnancy_result {
                    sqlx::query(
                        "INSERT OR IGNORE INTO pregnancies
                         (animal_id, result, checked_at, notes)
                         VALUES (?, ?, ?, ?)",
                    )
                    .bind(animal_id)
                    .bind(result)
                    .bind(&scan.scanned_at)
                    .bind(&scan.notes)
                    .execute(pool)
                    .await?;
                }
            }
            "test" => {
                if let Some(ref result) = scan.test_result {
                    sqlx::query(
                        "INSERT OR IGNORE INTO tests
                         (animal_id, test_name, result, tested_at, notes)
                         VALUES (?, ?, ?, ?, ?)",
                    )
                    .bind(animal_id)
                    .bind(scan.test_name.as_deref().unwrap_or(""))
                    .bind(result)
                    .bind(&scan.scanned_at)
                    .bind(&scan.notes)
                    .execute(pool)
                    .await?;
                }
            }
            "vaccination" => {
                if !scan.vaccines.is_empty() {
                    for vaccine in scan.vaccines.split(',') {
                        let vaccine = vaccine.trim();
                        if vaccine.is_empty() {
                            continue;
                        }
                        sqlx::query(
                            "INSERT OR IGNORE INTO vaccinations
                             (animal_id, vaccine, administered_at, notes)
                             VALUES (?, ?, ?, ?)",
                        )
                        .bind(animal_id)
                        .bind(vaccine)
                        .bind(&scan.scanned_at)
                        .bind(&scan.notes)
                        .execute(pool)
                        .await?;
                    }
                }
            }
            "removal" => {
                sqlx::query(
                    "INSERT OR IGNORE INTO removals (animal_id, reason, removed_at, notes)
                     VALUES (?, 'other', ?, ?)",
                )
                .bind(animal_id)
                .bind(&scan.scanned_at)
                .bind(&scan.notes)
                .execute(pool)
                .await?;

                sqlx::query(
                    "UPDATE animals SET is_active = 0,
                     updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
                     WHERE id = ?",
                )
                .bind(animal_id)
                .execute(pool)
                .await?;
            }
            _ => {}
        }
    }

    Ok(())
}

#[derive(FromRow)]
struct OrphanSessionRecord {
    session_id: i64,
    session_type: i64,
    scanned_at: String,
    event_data: String,
    note: String,
}

/// Counterpart to backfill_scan_events, for the session-based sync path
/// (routes/sessions.rs). That path's own fan_out_session_record only runs
/// at sync time and only if the tag is already linked to an animal — a tag
/// scanned before it's registered would otherwise never get its weight/
/// vaccination/pregnancy/test row created at all, since nothing re-visits
/// session_records later. Mirrors fan_out_session_record's logic (SQL and
/// all — including stamping session_id/eid so a per-tag voice note stays
/// linked and playable from the Animal Detail page, see migration 0006)
/// rather than sharing code with it, matching backfill_scan_events' own
/// self-contained style above. The INSERT ... ON CONFLICT DO UPDATE means
/// this is safe to run even if backfill_scan_events already created the
/// same row via the legacy scan_events path — it just backfills the
/// session_id/eid link onto it.
pub async fn backfill_session_records(
    pool: &SqlitePool,
    animal_id: i64,
    tag_number: &str,
) -> sqlx::Result<()> {
    let records = sqlx::query_as::<_, OrphanSessionRecord>(
        "SELECT r.session_id, s.type AS session_type, r.scanned_at, r.event_data, r.note
         FROM session_records r
         JOIN sessions s ON s.id = r.session_id
         WHERE r.eid = ?",
    )
    .bind(tag_number)
    .fetch_all(pool)
    .await?;

    for rec in &records {
        let data: Value = serde_json::from_str(&rec.event_data).unwrap_or(Value::Null);

        match rec.session_type {
            1 => {
                // Weighing
                if let Some(w) = data.get("weight_kg").and_then(Value::as_f64) {
                    if w > 0.0 {
                        sqlx::query(
                            "INSERT INTO weights
                             (animal_id, weight_kg, weighed_at, notes, session_id, eid)
                             VALUES (?, ?, ?, ?, ?, ?)
                             ON CONFLICT(animal_id, weighed_at) DO UPDATE SET
                                 session_id = excluded.session_id,
                                 eid        = excluded.eid",
                        )
                        .bind(animal_id)
                        .bind(w)
                        .bind(&rec.scanned_at)
                        .bind(&rec.note)
                        .bind(rec.session_id)
                        .bind(tag_number)
                        .execute(pool)
                        .await?;
                    }
                }
            }
            2 => {
                // Vaccination — one row per comma-separated vaccine name.
                if let Some(vaccines) = data.get("vaccines").and_then(Value::as_str) {
                    for vaccine in vaccines.split(',') {
                        let vaccine = vaccine.trim();
                        if vaccine.is_empty() {
                            continue;
                        }
                        sqlx::query(
                            "INSERT INTO vaccinations
                             (animal_id, vaccine, administered_at, notes, session_id, eid)
                             VALUES (?, ?, ?, ?, ?, ?)
                             ON CONFLICT(animal_id, vaccine, administered_at) DO UPDATE SET
                                 session_id = excluded.session_id,
                                 eid        = excluded.eid",
                        )
                        .bind(animal_id)
                        .bind(vaccine)
                        .bind(&rec.scanned_at)
                        .bind(&rec.note)
                        .bind(rec.session_id)
                        .bind(tag_number)
                        .execute(pool)
                        .await?;
                    }
                }
            }
            3 => {
                // Pregnancy
                if let Some(result) = data.get("pregnancy").and_then(Value::as_str) {
                    sqlx::query(
                        "INSERT INTO pregnancies
                         (animal_id, result, checked_at, notes, session_id, eid)
                         VALUES (?, ?, ?, ?, ?, ?)
                         ON CONFLICT(animal_id, checked_at) DO UPDATE SET
                             session_id = excluded.session_id,
                             eid        = excluded.eid",
                    )
                    .bind(animal_id)
                    .bind(result)
                    .bind(&rec.scanned_at)
                    .bind(&rec.note)
                    .bind(rec.session_id)
                    .bind(tag_number)
                    .execute(pool)
                    .await?;
                }
            }
            4 => {
                // Test
                if let Some(result) = data.get("test_result").and_then(Value::as_str) {
                    let test_name = data.get("test_name").and_then(Value::as_str).unwrap_or("");
                    sqlx::query(
                        "INSERT INTO tests
                         (animal_id, test_name, result, tested_at, notes, session_id, eid)
                         VALUES (?, ?, ?, ?, ?, ?, ?)
                         ON CONFLICT(animal_id, tested_at) DO UPDATE SET
                             session_id = excluded.session_id,
                             eid        = excluded.eid",
                    )
                    .bind(animal_id)
                    .bind(test_name)
                    .bind(result)
                    .bind(&rec.scanned_at)
                    .bind(&rec.note)
                    .bind(rec.session_id)
                    .bind(tag_number)
                    .execute(pool)
                    .await?;
                }
            }
            5 => {
                // Removal — no session_id/eid column on this table (see
                // migration 0006's scope); INSERT OR IGNORE, same as
                // fan_out_session_record's handling of this type.
                sqlx::query(
                    "INSERT OR IGNORE INTO removals (animal_id, reason, removed_at, notes)
                     VALUES (?, 'other', ?, ?)",
                )
                .bind(animal_id)
                .bind(&rec.scanned_at)
                .bind(&rec.note)
                .execute(pool)
                .await?;

                sqlx::query(
                    "UPDATE animals SET is_active = 0,
                     updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
                     WHERE id = ?",
                )
                .bind(animal_id)
                .execute(pool)
                .await?;
            }
            _ => {}
        }
    }

    Ok(())
}
