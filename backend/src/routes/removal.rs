use axum::{
    extract::{Path, State},
    http::StatusCode,
    Json,
};
use serde::Deserialize;
use sqlx::SqlitePool;

use crate::{
    error::{AppError, Result},
    models::{CreateRemoval, Removal},
};

#[derive(Debug, Deserialize)]
pub struct PatchRemoval {
    pub reason: Option<String>,
    pub removed_at: Option<String>,
    pub notes: Option<String>,
}

pub async fn create_removal(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
    Json(body): Json<CreateRemoval>,
) -> Result<(StatusCode, Json<Removal>)> {
    // Verify animal exists
    let exists: bool = sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM animals WHERE id = ?)")
        .bind(animal_id)
        .fetch_one(&pool)
        .await?;
    if !exists {
        return Err(AppError::NotFound);
    }

    let allowed = ["sold", "died", "slaughtered", "other"];
    if !allowed.contains(&body.reason.as_str()) {
        return Err(AppError::BadRequest(
            "reason must be sold, died, slaughtered, or other".into(),
        ));
    }

    // Insert removal record (UNIQUE on animal_id — will 409 if already removed)
    let removal = sqlx::query_as::<_, Removal>(
        "INSERT INTO removals (animal_id, reason, removed_at, notes)
         VALUES (?, ?, ?, ?)
         RETURNING *",
    )
    .bind(animal_id)
    .bind(&body.reason)
    .bind(&body.removed_at)
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;

    // Mark animal inactive
    sqlx::query(
        "UPDATE animals SET is_active = 0,
         updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
         WHERE id = ?",
    )
    .bind(animal_id)
    .execute(&pool)
    .await?;

    Ok((StatusCode::CREATED, Json(removal)))
}

pub async fn patch_removal(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
    Json(body): Json<PatchRemoval>,
) -> Result<Json<Removal>> {
    let existing = sqlx::query_as::<_, Removal>(
        "SELECT * FROM removals WHERE animal_id = ?",
    )
    .bind(animal_id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    if let Some(ref r) = body.reason {
        let allowed = ["sold", "died", "slaughtered", "other"];
        if !allowed.contains(&r.as_str()) {
            return Err(AppError::BadRequest(
                "reason must be sold, died, slaughtered, or other".into(),
            ));
        }
    }

    let reason     = body.reason.unwrap_or(existing.reason);
    let removed_at = body.removed_at.unwrap_or(existing.removed_at);
    let notes      = body.notes.unwrap_or(existing.notes);

    let removal = sqlx::query_as::<_, Removal>(
        "UPDATE removals SET reason = ?, removed_at = ?, notes = ?
         WHERE animal_id = ?
         RETURNING *",
    )
    .bind(&reason)
    .bind(&removed_at)
    .bind(&notes)
    .bind(animal_id)
    .fetch_one(&pool)
    .await?;

    Ok(Json(removal))
}

pub async fn delete_removal(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
) -> Result<StatusCode> {
    let deleted = sqlx::query("DELETE FROM removals WHERE animal_id = ?")
        .bind(animal_id)
        .execute(&pool)
        .await?;

    if deleted.rows_affected() == 0 {
        return Err(AppError::NotFound);
    }

    sqlx::query(
        "UPDATE animals SET is_active = 1,
         updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
         WHERE id = ?",
    )
    .bind(animal_id)
    .execute(&pool)
    .await?;

    Ok(StatusCode::NO_CONTENT)
}
