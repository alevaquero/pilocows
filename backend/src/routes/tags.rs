use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use sqlx::SqlitePool;

use crate::{
    error::{AppError, Result},
    models::{CreateTag, Tag, TagQuery},
};

pub async fn list_tags(
    State(pool): State<SqlitePool>,
    Query(q): Query<TagQuery>,
) -> Result<Json<Vec<Tag>>> {
    let tags = if q.unassigned == Some(true) {
        sqlx::query_as::<_, Tag>(
            "SELECT t.* FROM tags t
             LEFT JOIN animals a ON a.tag_id = t.id AND a.is_active = 1
             WHERE a.id IS NULL
             ORDER BY t.tag_number",
        )
        .fetch_all(&pool)
        .await?
    } else {
        sqlx::query_as::<_, Tag>("SELECT * FROM tags ORDER BY tag_number")
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
