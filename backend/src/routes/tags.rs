use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use sqlx::SqlitePool;

use crate::{
    error::{AppError, Result},
    models::{CreateTag, Tag, TagQuery, TagWithStatus},
};

pub async fn list_tags(
    State(pool): State<SqlitePool>,
    Query(q): Query<TagQuery>,
) -> Result<Json<Vec<TagWithStatus>>> {
    let tags = if q.unassigned == Some(true) {
        sqlx::query_as::<_, TagWithStatus>(
            "SELECT t.id, t.tag_number, t.purchased_at, t.notes, t.created_at,
                    'available' AS animal_status,
                    NULL AS animal_id
             FROM tags t
             WHERE NOT EXISTS (SELECT 1 FROM animals WHERE tag_id = t.id)
             ORDER BY t.tag_number",
        )
        .fetch_all(&pool)
        .await?
    } else {
        sqlx::query_as::<_, TagWithStatus>(
            "SELECT t.id, t.tag_number, t.purchased_at, t.notes, t.created_at,
                    CASE
                        WHEN EXISTS (
                            SELECT 1 FROM animals WHERE tag_id = t.id AND is_active = 1
                        ) THEN 'active'
                        WHEN EXISTS (
                            SELECT 1 FROM animals a
                            JOIN removals r ON r.animal_id = a.id AND r.reason = 'sold'
                            WHERE a.tag_id = t.id
                        ) THEN 'sold'
                        WHEN EXISTS (
                            SELECT 1 FROM animals WHERE tag_id = t.id
                        ) THEN 'retired'
                        ELSE 'available'
                    END AS animal_status,
                    (SELECT a.id FROM animals a
                     WHERE a.tag_id = t.id AND a.is_active = 1 LIMIT 1) AS animal_id
             FROM tags t
             ORDER BY t.tag_number",
        )
        .fetch_all(&pool)
        .await?
    };
    Ok(Json(tags))
}

pub async fn get_tag_by_number(
    State(pool): State<SqlitePool>,
    Path(tag_number): Path<String>,
) -> Result<Json<Tag>> {
    let tag = sqlx::query_as::<_, Tag>("SELECT * FROM tags WHERE tag_number = ?")
        .bind(&tag_number)
        .fetch_optional(&pool)
        .await?
        .ok_or(AppError::NotFound)?;
    Ok(Json(tag))
}

pub async fn delete_tag(
    State(pool): State<SqlitePool>,
    Path(tag_number): Path<String>,
) -> Result<StatusCode> {
    let tag = sqlx::query_as::<_, Tag>("SELECT * FROM tags WHERE tag_number = ?")
        .bind(&tag_number)
        .fetch_optional(&pool)
        .await?
        .ok_or(AppError::NotFound)?;

    let has_animal: bool =
        sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM animals WHERE tag_id = ?)")
            .bind(tag.id)
            .fetch_one(&pool)
            .await?;
    if has_animal {
        return Err(AppError::Conflict(
            "Tag is assigned to an animal — delete the animal first".into(),
        ));
    }

    sqlx::query("DELETE FROM tags WHERE id = ?")
        .bind(tag.id)
        .execute(&pool)
        .await?;
    Ok(StatusCode::NO_CONTENT)
}

pub async fn create_tag(
    State(pool): State<SqlitePool>,
    Json(body): Json<CreateTag>,
) -> Result<(StatusCode, Json<Tag>)> {
    let tag = sqlx::query_as::<_, Tag>(
        "INSERT INTO tags (tag_number, purchased_at, notes)
         VALUES (?, ?, ?)
         RETURNING *",
    )
    .bind(&body.tag_number)
    .bind(&body.purchased_at)
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;
    Ok((StatusCode::CREATED, Json(tag)))
}
