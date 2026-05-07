use axum::{
    extract::{Path, State},
    http::StatusCode,
    Json,
};
use sqlx::SqlitePool;

use crate::{
    error::{AppError, Result},
    models::{
        CreatePregnancy, CreateTbTest, CreateVaccination, CreateWeight,
        PatchPregnancy, PatchTbTest, PatchVaccination, PatchWeight,
        Pregnancy, TbTest, Vaccination, Weight,
    },
};

// ─── helpers ──────────────────────────────────────────────────────────────────

async fn animal_exists(pool: &SqlitePool, animal_id: i64) -> Result<()> {
    let exists: bool = sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM animals WHERE id = ?)")
        .bind(animal_id)
        .fetch_one(pool)
        .await?;
    if !exists {
        Err(AppError::NotFound)
    } else {
        Ok(())
    }
}

// ─── Vaccinations ─────────────────────────────────────────────────────────────

pub async fn list_vaccinations(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
) -> Result<Json<Vec<Vaccination>>> {
    animal_exists(&pool, animal_id).await?;
    let rows = sqlx::query_as::<_, Vaccination>(
        "SELECT * FROM vaccinations WHERE animal_id = ? ORDER BY administered_at DESC",
    )
    .bind(animal_id)
    .fetch_all(&pool)
    .await?;
    Ok(Json(rows))
}

pub async fn create_vaccination(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
    Json(body): Json<CreateVaccination>,
) -> Result<(StatusCode, Json<Vaccination>)> {
    animal_exists(&pool, animal_id).await?;
    let row = sqlx::query_as::<_, Vaccination>(
        "INSERT INTO vaccinations (animal_id, vaccine, dose, administered_at, next_due_at, notes)
         VALUES (?, ?, ?, ?, ?, ?)
         RETURNING *",
    )
    .bind(animal_id)
    .bind(&body.vaccine)
    .bind(&body.dose)
    .bind(&body.administered_at)
    .bind(body.next_due_at.as_deref())
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;
    Ok((StatusCode::CREATED, Json(row)))
}

pub async fn patch_vaccination(
    State(pool): State<SqlitePool>,
    Path((animal_id, vaccination_id)): Path<(i64, i64)>,
    Json(body): Json<PatchVaccination>,
) -> Result<Json<Vaccination>> {
    let existing = sqlx::query_as::<_, Vaccination>(
        "SELECT * FROM vaccinations WHERE id = ? AND animal_id = ?",
    )
    .bind(vaccination_id)
    .bind(animal_id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    let row = sqlx::query_as::<_, Vaccination>(
        "UPDATE vaccinations SET vaccine = ?, dose = ?, administered_at = ?,
         next_due_at = ?, notes = ? WHERE id = ? RETURNING *",
    )
    .bind(body.vaccine.unwrap_or(existing.vaccine))
    .bind(body.dose.unwrap_or(existing.dose))
    .bind(body.administered_at.unwrap_or(existing.administered_at))
    .bind(body.next_due_at.or(existing.next_due_at))
    .bind(body.notes.unwrap_or(existing.notes))
    .bind(vaccination_id)
    .fetch_one(&pool)
    .await?;
    Ok(Json(row))
}

pub async fn delete_vaccination(
    State(pool): State<SqlitePool>,
    Path((animal_id, vaccination_id)): Path<(i64, i64)>,
) -> Result<StatusCode> {
    let deleted = sqlx::query("DELETE FROM vaccinations WHERE id = ? AND animal_id = ?")
        .bind(vaccination_id)
        .bind(animal_id)
        .execute(&pool)
        .await?;
    if deleted.rows_affected() == 0 {
        return Err(AppError::NotFound);
    }
    Ok(StatusCode::NO_CONTENT)
}

// ─── Pregnancies ──────────────────────────────────────────────────────────────

pub async fn list_pregnancies(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
) -> Result<Json<Vec<Pregnancy>>> {
    animal_exists(&pool, animal_id).await?;
    let rows = sqlx::query_as::<_, Pregnancy>(
        "SELECT * FROM pregnancies WHERE animal_id = ? ORDER BY checked_at DESC",
    )
    .bind(animal_id)
    .fetch_all(&pool)
    .await?;
    Ok(Json(rows))
}

pub async fn create_pregnancy(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
    Json(body): Json<CreatePregnancy>,
) -> Result<(StatusCode, Json<Pregnancy>)> {
    animal_exists(&pool, animal_id).await?;
    let allowed = ["unknown", "not_pregnant", "small_pregnant", "medium_pregnant", "big_pregnant", "rejected"];
    if !allowed.contains(&body.result.as_str()) {
        return Err(AppError::BadRequest(
            "result must be one of: unknown, not_pregnant, small_pregnant, medium_pregnant, big_pregnant, rejected".into(),
        ));
    }
    let row = sqlx::query_as::<_, Pregnancy>(
        "INSERT INTO pregnancies (animal_id, result, checked_at, due_date, notes)
         VALUES (?, ?, ?, ?, ?)
         RETURNING *",
    )
    .bind(animal_id)
    .bind(&body.result)
    .bind(&body.checked_at)
    .bind(body.due_date.as_deref())
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;
    Ok((StatusCode::CREATED, Json(row)))
}

pub async fn patch_pregnancy(
    State(pool): State<SqlitePool>,
    Path((animal_id, pregnancy_id)): Path<(i64, i64)>,
    Json(body): Json<PatchPregnancy>,
) -> Result<Json<Pregnancy>> {
    let existing = sqlx::query_as::<_, Pregnancy>(
        "SELECT * FROM pregnancies WHERE id = ? AND animal_id = ?",
    )
    .bind(pregnancy_id)
    .bind(animal_id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    if let Some(ref r) = body.result {
        let allowed = ["unknown", "not_pregnant", "small_pregnant", "medium_pregnant", "big_pregnant", "rejected"];
        if !allowed.contains(&r.as_str()) {
            return Err(AppError::BadRequest(
                "result must be one of: unknown, not_pregnant, small_pregnant, medium_pregnant, big_pregnant, rejected".into(),
            ));
        }
    }

    let row = sqlx::query_as::<_, Pregnancy>(
        "UPDATE pregnancies SET result = ?, checked_at = ?, due_date = ?, notes = ?
         WHERE id = ? RETURNING *",
    )
    .bind(body.result.unwrap_or(existing.result))
    .bind(body.checked_at.unwrap_or(existing.checked_at))
    .bind(body.due_date.or(existing.due_date))
    .bind(body.notes.unwrap_or(existing.notes))
    .bind(pregnancy_id)
    .fetch_one(&pool)
    .await?;
    Ok(Json(row))
}

pub async fn delete_pregnancy(
    State(pool): State<SqlitePool>,
    Path((animal_id, pregnancy_id)): Path<(i64, i64)>,
) -> Result<StatusCode> {
    let deleted = sqlx::query("DELETE FROM pregnancies WHERE id = ? AND animal_id = ?")
        .bind(pregnancy_id)
        .bind(animal_id)
        .execute(&pool)
        .await?;
    if deleted.rows_affected() == 0 {
        return Err(AppError::NotFound);
    }
    Ok(StatusCode::NO_CONTENT)
}

// ─── TB Tests ─────────────────────────────────────────────────────────────────

pub async fn list_tb_tests(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
) -> Result<Json<Vec<TbTest>>> {
    animal_exists(&pool, animal_id).await?;
    let rows = sqlx::query_as::<_, TbTest>(
        "SELECT * FROM tb_tests WHERE animal_id = ? ORDER BY tested_at DESC",
    )
    .bind(animal_id)
    .fetch_all(&pool)
    .await?;
    Ok(Json(rows))
}

pub async fn create_tb_test(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
    Json(body): Json<CreateTbTest>,
) -> Result<(StatusCode, Json<TbTest>)> {
    animal_exists(&pool, animal_id).await?;
    let allowed = ["positive", "negative", "inconclusive"];
    if !allowed.contains(&body.result.as_str()) {
        return Err(AppError::BadRequest(
            "result must be positive, negative, or inconclusive".into(),
        ));
    }
    let row = sqlx::query_as::<_, TbTest>(
        "INSERT INTO tb_tests (animal_id, result, tested_at, notes)
         VALUES (?, ?, ?, ?)
         RETURNING *",
    )
    .bind(animal_id)
    .bind(&body.result)
    .bind(&body.tested_at)
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;
    Ok((StatusCode::CREATED, Json(row)))
}

pub async fn patch_tb_test(
    State(pool): State<SqlitePool>,
    Path((animal_id, tb_test_id)): Path<(i64, i64)>,
    Json(body): Json<PatchTbTest>,
) -> Result<Json<TbTest>> {
    let existing = sqlx::query_as::<_, TbTest>(
        "SELECT * FROM tb_tests WHERE id = ? AND animal_id = ?",
    )
    .bind(tb_test_id)
    .bind(animal_id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    if let Some(ref r) = body.result {
        let allowed = ["positive", "negative", "inconclusive"];
        if !allowed.contains(&r.as_str()) {
            return Err(AppError::BadRequest(
                "result must be positive, negative, or inconclusive".into(),
            ));
        }
    }

    let row = sqlx::query_as::<_, TbTest>(
        "UPDATE tb_tests SET result = ?, tested_at = ?, notes = ?
         WHERE id = ? RETURNING *",
    )
    .bind(body.result.unwrap_or(existing.result))
    .bind(body.tested_at.unwrap_or(existing.tested_at))
    .bind(body.notes.unwrap_or(existing.notes))
    .bind(tb_test_id)
    .fetch_one(&pool)
    .await?;
    Ok(Json(row))
}

pub async fn delete_tb_test(
    State(pool): State<SqlitePool>,
    Path((animal_id, tb_test_id)): Path<(i64, i64)>,
) -> Result<StatusCode> {
    let deleted = sqlx::query("DELETE FROM tb_tests WHERE id = ? AND animal_id = ?")
        .bind(tb_test_id)
        .bind(animal_id)
        .execute(&pool)
        .await?;
    if deleted.rows_affected() == 0 {
        return Err(AppError::NotFound);
    }
    Ok(StatusCode::NO_CONTENT)
}

// ─── Weights ──────────────────────────────────────────────────────────────────

pub async fn list_weights(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
) -> Result<Json<Vec<Weight>>> {
    animal_exists(&pool, animal_id).await?;
    let rows = sqlx::query_as::<_, Weight>(
        "SELECT * FROM weights WHERE animal_id = ? ORDER BY weighed_at DESC",
    )
    .bind(animal_id)
    .fetch_all(&pool)
    .await?;
    Ok(Json(rows))
}

pub async fn create_weight(
    State(pool): State<SqlitePool>,
    Path(animal_id): Path<i64>,
    Json(body): Json<CreateWeight>,
) -> Result<(StatusCode, Json<Weight>)> {
    animal_exists(&pool, animal_id).await?;
    if body.weight_kg <= 0.0 {
        return Err(AppError::BadRequest("weight_kg must be positive".into()));
    }
    let row = sqlx::query_as::<_, Weight>(
        "INSERT INTO weights (animal_id, weight_kg, weighed_at, notes)
         VALUES (?, ?, ?, ?)
         RETURNING *",
    )
    .bind(animal_id)
    .bind(body.weight_kg)
    .bind(&body.weighed_at)
    .bind(&body.notes)
    .fetch_one(&pool)
    .await?;
    Ok((StatusCode::CREATED, Json(row)))
}

pub async fn patch_weight(
    State(pool): State<SqlitePool>,
    Path((animal_id, weight_id)): Path<(i64, i64)>,
    Json(body): Json<PatchWeight>,
) -> Result<Json<Weight>> {
    let existing = sqlx::query_as::<_, Weight>(
        "SELECT * FROM weights WHERE id = ? AND animal_id = ?",
    )
    .bind(weight_id)
    .bind(animal_id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    if let Some(w) = body.weight_kg {
        if w <= 0.0 {
            return Err(AppError::BadRequest("weight_kg must be positive".into()));
        }
    }

    let row = sqlx::query_as::<_, Weight>(
        "UPDATE weights SET weight_kg = ?, weighed_at = ?, notes = ?
         WHERE id = ? RETURNING *",
    )
    .bind(body.weight_kg.unwrap_or(existing.weight_kg))
    .bind(body.weighed_at.unwrap_or(existing.weighed_at))
    .bind(body.notes.unwrap_or(existing.notes))
    .bind(weight_id)
    .fetch_one(&pool)
    .await?;
    Ok(Json(row))
}

pub async fn delete_weight(
    State(pool): State<SqlitePool>,
    Path((animal_id, weight_id)): Path<(i64, i64)>,
) -> Result<StatusCode> {
    let deleted = sqlx::query("DELETE FROM weights WHERE id = ? AND animal_id = ?")
        .bind(weight_id)
        .bind(animal_id)
        .execute(&pool)
        .await?;
    if deleted.rows_affected() == 0 {
        return Err(AppError::NotFound);
    }
    Ok(StatusCode::NO_CONTENT)
}
