use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    Json,
};
use sqlx::SqlitePool;

use crate::{
    db,
    error::{AppError, Result},
    models::{
        Animal, AnimalProfile, AnimalQuery, AnimalWithTag, CreateAnimal, PatchAnimal, Pregnancy,
        Removal, Test, Vaccination, Weight,
    },
};

pub async fn list_animals(
    State(pool): State<SqlitePool>,
    Query(q): Query<AnimalQuery>,
) -> Result<Json<Vec<AnimalWithTag>>> {
    // Build query dynamically based on filters
    let mut sql = String::from(
        "SELECT a.*, t.tag_number FROM animals a
         JOIN tags t ON t.id = a.tag_id
         WHERE 1=1",
    );
    let mut binds: Vec<String> = Vec::new();

    if let Some(ref tn) = q.tag_number {
        sql.push_str(" AND t.tag_number = ?");
        binds.push(tn.clone());
    }
    if let Some(ref cat) = q.category {
        sql.push_str(" AND a.category = ?");
        binds.push(cat.clone());
    }
    if let Some(active) = q.is_active {
        sql.push_str(" AND a.is_active = ?");
        binds.push(if active { "1".into() } else { "0".into() });
    }
    sql.push_str(" ORDER BY a.id DESC");

    let mut query = sqlx::query_as::<_, AnimalWithTag>(&sql);
    for b in &binds {
        query = query.bind(b);
    }
    let animals = query.fetch_all(&pool).await?;
    Ok(Json(animals))
}

pub async fn get_animal(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
) -> Result<Json<AnimalProfile>> {
    let animal = sqlx::query_as::<_, AnimalWithTag>(
        "SELECT a.*, t.tag_number FROM animals a
         JOIN tags t ON t.id = a.tag_id
         WHERE a.id = ?",
    )
    .bind(id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    let vaccinations = sqlx::query_as::<_, Vaccination>(
        "SELECT * FROM vaccinations WHERE animal_id = ? ORDER BY administered_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let pregnancies = sqlx::query_as::<_, Pregnancy>(
        "SELECT * FROM pregnancies WHERE animal_id = ? ORDER BY checked_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let tests = sqlx::query_as::<_, Test>(
        "SELECT * FROM tests WHERE animal_id = ? ORDER BY tested_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let weights = sqlx::query_as::<_, Weight>(
        "SELECT * FROM weights WHERE animal_id = ? ORDER BY weighed_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let removal = sqlx::query_as::<_, Removal>("SELECT * FROM removals WHERE animal_id = ?")
        .bind(id)
        .fetch_optional(&pool)
        .await?;

    Ok(Json(AnimalProfile {
        animal,
        vaccinations,
        pregnancies,
        tests,
        weights,
        removal,
    }))
}

pub async fn create_animal(
    State(pool): State<SqlitePool>,
    Json(body): Json<CreateAnimal>,
) -> Result<(StatusCode, Json<Animal>)> {
    // Verify tag exists
    let exists: bool =
        sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM tags WHERE id = ?)")
            .bind(body.tag_id)
            .fetch_one(&pool)
            .await?;
    if !exists {
        return Err(AppError::BadRequest("tag_id does not exist".into()));
    }

    // Resolve tag_number before the insert so we can backfill scan_events after
    let tag_number: String =
        sqlx::query_scalar("SELECT tag_number FROM tags WHERE id = ?")
            .bind(body.tag_id)
            .fetch_one(&pool)
            .await?;

    let animal = sqlx::query_as::<_, Animal>(
        "INSERT INTO animals (tag_id, breed, category, sex, dob, notes)
         VALUES (?, ?, ?, ?, ?, ?)
         RETURNING *",
    )
    .bind(body.tag_id)
    .bind(&body.breed)
    .bind(&body.category)
    .bind(&body.sex)
    .bind(body.dob.as_deref())
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;

    // Link any scan_events that arrived before this animal was registered and
    // fan them out into the typed health tables (weights, vaccinations, etc.)
    db::backfill_scan_events(&pool, animal.id, &tag_number).await?;

    Ok((StatusCode::CREATED, Json(animal)))
}

pub async fn delete_animal(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
) -> Result<StatusCode> {
    let exists: bool = sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM animals WHERE id = ?)")
        .bind(id)
        .fetch_one(&pool)
        .await?;
    if !exists {
        return Err(AppError::NotFound);
    }
    sqlx::query("DELETE FROM animals WHERE id = ?")
        .bind(id)
        .execute(&pool)
        .await?;
    Ok(StatusCode::NO_CONTENT)
}

pub async fn patch_animal(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
    Json(body): Json<PatchAnimal>,
) -> Result<Json<Animal>> {
    // Check animal exists
    let existing = sqlx::query_as::<_, Animal>("SELECT * FROM animals WHERE id = ?")
        .bind(id)
        .fetch_optional(&pool)
        .await?
        .ok_or(AppError::NotFound)?;

    let breed = body.breed.unwrap_or(existing.breed);
    let category = body.category.unwrap_or(existing.category);
    let sex = body.sex.unwrap_or(existing.sex);
    let dob = body.dob.or(existing.dob);
    let notes = body.notes.unwrap_or(existing.notes);
    let is_active = body
        .is_active
        .map(|v| if v { 1i64 } else { 0i64 })
        .unwrap_or(existing.is_active);

    let animal = sqlx::query_as::<_, Animal>(
        "UPDATE animals
         SET breed = ?, category = ?, sex = ?, dob = ?, notes = ?,
             is_active = ?,
             updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
         WHERE id = ?
         RETURNING *",
    )
    .bind(&breed)
    .bind(&category)
    .bind(&sex)
    .bind(dob.as_deref())
    .bind(&notes)
    .bind(is_active)
    .bind(id)
    .fetch_one(&pool)
    .await?;

    Ok(Json(animal))
}
