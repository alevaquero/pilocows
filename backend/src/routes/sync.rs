use axum::{extract::State, http::StatusCode, Json};
use serde::Serialize;
use sqlx::SqlitePool;

use crate::{
    error::Result,
    models::{IncomingScan, ScanEvent},
};

#[derive(Debug, Serialize)]
pub struct SyncResponse {
    pub accepted: usize,
    /// EIDs that were not found in the animals table — need registration in the frontend.
    pub unregistered_eids: Vec<String>,
}

pub async fn sync_scans(
    State(pool): State<SqlitePool>,
    Json(scans): Json<Vec<IncomingScan>>,
) -> Result<(StatusCode, Json<SyncResponse>)> {
    let mut unregistered_eids: Vec<String> = Vec::new();
    let mut accepted = 0usize;

    for scan in &scans {
        // A tag is "unregistered" only if it doesn't exist in the tags table at all.
        // A tag that exists but has no animal assigned is already registered — the user
        // can assign it to an animal later via the Tags UI.  Using the animal-join as
        // the check caused every tag-without-animal to appear as unregistered on every
        // sync, making "Register Tag" fail with "already exists" on re-syncs.
        let tag_known: bool = sqlx::query_scalar::<_, i64>(
            "SELECT COUNT(*) FROM tags WHERE tag_number = ?",
        )
        .bind(&scan.eid)
        .fetch_one(&pool)
        .await
        .map(|n| n > 0)?;

        if !tag_known && !unregistered_eids.contains(&scan.eid) {
            unregistered_eids.push(scan.eid.clone());
        }

        // Resolve animal_id separately — needed for fan_out into health tables.
        let animal_id: Option<i64> = sqlx::query_scalar(
            "SELECT a.id FROM animals a
             JOIN tags t ON t.id = a.tag_id
             WHERE t.tag_number = ? AND a.is_active = 1
             LIMIT 1",
        )
        .bind(&scan.eid)
        .fetch_optional(&pool)
        .await?;

        // ── 1. Raw scan log ───────────────────────────────────────────────────
        // INSERT OR IGNORE is idempotent via idx_scan_events_dedup (eid, scanned_at).
        sqlx::query(
            "INSERT OR IGNORE INTO scan_events
             (eid, event_type, scanned_at, weight_kg, pregnancy_result, tb_result, vaccines, notes, animal_id)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        )
        .bind(&scan.eid)
        .bind(&scan.event_type)
        .bind(&scan.scanned_at)
        .bind(scan.weight_kg)
        .bind(scan.pregnancy_result.as_deref())
        .bind(scan.tb_result.as_deref())
        .bind(&scan.vaccines)
        .bind(&scan.notes)
        .bind(animal_id)
        .execute(&pool)
        .await?;

        // ── 2. Fan out into typed health tables (only when animal is known) ───
        if let Some(aid) = animal_id {
            fan_out(&pool, scan, aid).await?;
        }

        accepted += 1;
    }

    Ok((
        StatusCode::OK,
        Json(SyncResponse {
            accepted,
            unregistered_eids,
        }),
    ))
}

/// Write a scan event into the appropriate health record table.
/// All inserts are INSERT OR IGNORE via the dedup indices added in migration 0002.
async fn fan_out(pool: &SqlitePool, scan: &IncomingScan, animal_id: i64) -> Result<()> {
    match scan.event_type.as_str() {
        "pregnancy" => {
            if let Some(result) = &scan.pregnancy_result {
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

        "weighing" => {
            if let Some(weight) = scan.weight_kg {
                if weight > 0.0 {
                    sqlx::query(
                        "INSERT OR IGNORE INTO weights
                         (animal_id, weight_kg, weighed_at, notes)
                         VALUES (?, ?, ?, ?)",
                    )
                    .bind(animal_id)
                    .bind(weight)
                    .bind(&scan.scanned_at)
                    .bind(&scan.notes)
                    .execute(pool)
                    .await?;
                }
            }
        }

        "tb_test" => {
            if let Some(result) = &scan.tb_result {
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
            // vaccines is a comma-separated list of names from the handheld.
            // Store one vaccination record per vaccine so each shows independently.
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
            // INSERT OR IGNORE: if the animal was already removed via the UI, leave it as-is.
            sqlx::query(
                "INSERT OR IGNORE INTO removals (animal_id, reason, removed_at, notes)
                 VALUES (?, 'other', ?, ?)",
            )
            .bind(animal_id)
            .bind(&scan.scanned_at)
            .bind(&scan.notes)
            .execute(pool)
            .await?;

            // Mark inactive regardless (idempotent).
            sqlx::query(
                "UPDATE animals SET is_active = 0,
                 updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
                 WHERE id = ?",
            )
            .bind(animal_id)
            .execute(pool)
            .await?;
        }

        // "general" carries no structured health data to fan out.
        _ => {}
    }

    Ok(())
}

pub async fn list_scan_events(
    State(pool): State<SqlitePool>,
) -> Result<Json<Vec<ScanEvent>>> {
    let rows = sqlx::query_as::<_, ScanEvent>(
        "SELECT * FROM scan_events ORDER BY scanned_at DESC",
    )
    .fetch_all(&pool)
    .await?;
    Ok(Json(rows))
}
