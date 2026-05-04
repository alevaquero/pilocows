use sqlx::{sqlite::SqlitePoolOptions, SqlitePool};

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
            "tb_test" => {
                if let Some(ref result) = scan.tb_result {
                    sqlx::query(
                        "INSERT OR IGNORE INTO tb_tests
                         (animal_id, result, tested_at, notes)
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
