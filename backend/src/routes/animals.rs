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
        AnimalProfile, AnimalQuery, AnimalWithTag, CreateAnimal, GeneralScan, PatchAnimal,
        Pregnancy, Removal, Test, Vaccination, Weight,
    },
};

// Shared by list/get/create/patch so every response shape is consistent —
// includes the father/mother EID resolved via a self-join, not just their
// internal ids.
const ANIMAL_WITH_TAG_SELECT: &str = "
    SELECT a.*, t.tag_number,
           ft.tag_number AS father_tag_number,
           mt.tag_number AS mother_tag_number
    FROM animals a
    JOIN tags t ON t.id = a.tag_id
    LEFT JOIN animals fa ON fa.id = a.father_id
    LEFT JOIN tags ft ON ft.id = fa.tag_id
    LEFT JOIN animals ma ON ma.id = a.mother_id
    LEFT JOIN tags mt ON mt.id = ma.tag_id";

async fn fetch_animal_with_tag(pool: &SqlitePool, id: i64) -> Result<AnimalWithTag> {
    let sql = format!("{ANIMAL_WITH_TAG_SELECT} WHERE a.id = ?");
    sqlx::query_as::<_, AnimalWithTag>(&sql)
        .bind(id)
        .fetch_optional(pool)
        .await?
        .ok_or(AppError::NotFound)
}

// Resolves a father_eid/mother_eid input to an animal id.
// Trimmed-empty (or omitted upstream) means "no parent" -> Ok(None).
// A non-empty EID must match an already-registered animal (i.e. a tag with
// an animals row, not just an inventory tag) or this errors.
async fn resolve_parent_eid(pool: &SqlitePool, eid: Option<&str>) -> Result<Option<i64>> {
    let eid = match eid.map(str::trim) {
        Some(e) if !e.is_empty() => e,
        _ => return Ok(None),
    };
    let id: Option<i64> = sqlx::query_scalar(
        "SELECT a.id FROM animals a JOIN tags t ON t.id = a.tag_id WHERE t.tag_number = ?",
    )
    .bind(eid)
    .fetch_optional(pool)
    .await?;
    id.map(Some)
        .ok_or_else(|| AppError::BadRequest(format!("no registered animal with EID '{eid}'")))
}

pub async fn list_animals(
    State(pool): State<SqlitePool>,
    Query(q): Query<AnimalQuery>,
) -> Result<Json<Vec<AnimalWithTag>>> {
    // Build query dynamically based on filters
    let mut sql = format!("{ANIMAL_WITH_TAG_SELECT} WHERE 1=1");
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
    let animal = fetch_animal_with_tag(&pool, id).await?;

    // Each of these LEFT JOINs back onto session_records via the
    // (session_id, eid) link fan_out_session_record() stamps on rows it
    // creates from a handheld sync (see migration 0006) — has_audio is
    // false/absent-audio for rows with no session link (created directly
    // via the frontend UI) since the JOIN simply finds nothing to match.
    let vaccinations = sqlx::query_as::<_, Vaccination>(
        "SELECT v.*, (sr.audio IS NOT NULL) AS has_audio
         FROM vaccinations v
         LEFT JOIN session_records sr ON sr.session_id = v.session_id AND sr.eid = v.eid
         WHERE v.animal_id = ? ORDER BY v.administered_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let pregnancies = sqlx::query_as::<_, Pregnancy>(
        "SELECT p.*, (sr.audio IS NOT NULL) AS has_audio
         FROM pregnancies p
         LEFT JOIN session_records sr ON sr.session_id = p.session_id AND sr.eid = p.eid
         WHERE p.animal_id = ? ORDER BY p.checked_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let tests = sqlx::query_as::<_, Test>(
        "SELECT te.*, (sr.audio IS NOT NULL) AS has_audio
         FROM tests te
         LEFT JOIN session_records sr ON sr.session_id = te.session_id AND sr.eid = te.eid
         WHERE te.animal_id = ? ORDER BY te.tested_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let weights = sqlx::query_as::<_, Weight>(
        "SELECT w.*, (sr.audio IS NOT NULL) AS has_audio
         FROM weights w
         LEFT JOIN session_records sr ON sr.session_id = w.session_id AND sr.eid = w.eid
         WHERE w.animal_id = ? ORDER BY w.weighed_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    let removal = sqlx::query_as::<_, Removal>("SELECT * FROM removals WHERE animal_id = ?")
        .bind(id)
        .fetch_optional(&pool)
        .await?;

    // General (session_type=0) scans have no health table of their own —
    // the only place this animal's General-session history is visible.
    let general_scans = sqlx::query_as::<_, GeneralScan>(
        "SELECT r.session_id, s.name AS session_name, r.eid, r.scanned_at, r.note,
                (r.audio IS NOT NULL) AS has_audio
         FROM session_records r
         JOIN sessions s ON s.id = r.session_id
         JOIN tags t ON t.tag_number = r.eid
         WHERE t.id = (SELECT tag_id FROM animals WHERE id = ?) AND s.type = 0
         ORDER BY r.scanned_at DESC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    Ok(Json(AnimalProfile {
        animal,
        vaccinations,
        pregnancies,
        tests,
        weights,
        removal,
        general_scans,
    }))
}

pub async fn create_animal(
    State(pool): State<SqlitePool>,
    Json(body): Json<CreateAnimal>,
) -> Result<(StatusCode, Json<AnimalWithTag>)> {
    // Verify tag exists
    let exists: bool =
        sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM tags WHERE id = ?)")
            .bind(body.tag_id)
            .fetch_one(&pool)
            .await?;
    if !exists {
        return Err(AppError::BadRequest("tag_id does not exist".into()));
    }

    let father_id = resolve_parent_eid(&pool, body.father_eid.as_deref()).await?;
    let mother_id = resolve_parent_eid(&pool, body.mother_eid.as_deref()).await?;

    // Resolve tag_number before the insert so we can backfill scan_events after
    let tag_number: String =
        sqlx::query_scalar("SELECT tag_number FROM tags WHERE id = ?")
            .bind(body.tag_id)
            .fetch_one(&pool)
            .await?;

    let id: i64 = sqlx::query_scalar(
        "INSERT INTO animals (tag_id, breed, category, sex, dob, notes, father_id, mother_id)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?)
         RETURNING id",
    )
    .bind(body.tag_id)
    .bind(&body.breed)
    .bind(&body.category)
    .bind(&body.sex)
    .bind(body.dob.as_deref())
    .bind(&body.notes)
    .bind(father_id)
    .bind(mother_id)
    .fetch_one(&pool)
    .await?;

    // Link any scan_events that arrived before this animal was registered and
    // fan them out into the typed health tables (weights, vaccinations, etc.)
    db::backfill_scan_events(&pool, id, &tag_number).await?;
    // Same, for the newer session-based sync path (session_records) — see
    // backfill_session_records' doc comment for why this is also needed.
    db::backfill_session_records(&pool, id, &tag_number).await?;

    // Re-fetch through the joined select (like list/get_animal already use)
    // rather than hand-building the response — the plain inserted row alone
    // left the frontend's list showing a blank EID/parent EID until the
    // next reload.
    let animal = fetch_animal_with_tag(&pool, id).await?;

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
) -> Result<Json<AnimalWithTag>> {
    // Check animal exists
    let existing = fetch_animal_with_tag(&pool, id).await?;

    let breed = body.breed.unwrap_or(existing.breed);
    let category = body.category.unwrap_or(existing.category);
    let sex = body.sex.unwrap_or(existing.sex);
    let dob = body.dob.or(existing.dob);
    let notes = body.notes.unwrap_or(existing.notes);
    let is_active = body
        .is_active
        .map(|v| if v { 1i64 } else { 0i64 })
        .unwrap_or(existing.is_active);

    // None = field omitted, leave unchanged; Some(eid) = re-resolve
    // (Some("") clears it — resolve_parent_eid maps empty to None).
    let father_id = match body.father_eid {
        Some(eid) => resolve_parent_eid(&pool, Some(&eid)).await?,
        None => existing.father_id,
    };
    let mother_id = match body.mother_eid {
        Some(eid) => resolve_parent_eid(&pool, Some(&eid)).await?,
        None => existing.mother_id,
    };
    if father_id == Some(id) || mother_id == Some(id) {
        return Err(AppError::BadRequest("an animal cannot be its own parent".into()));
    }

    sqlx::query(
        "UPDATE animals
         SET breed = ?, category = ?, sex = ?, dob = ?, notes = ?,
             is_active = ?, father_id = ?, mother_id = ?,
             updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
         WHERE id = ?",
    )
    .bind(&breed)
    .bind(&category)
    .bind(&sex)
    .bind(dob.as_deref())
    .bind(&notes)
    .bind(is_active)
    .bind(father_id)
    .bind(mother_id)
    .bind(id)
    .execute(&pool)
    .await?;

    let animal = fetch_animal_with_tag(&pool, id).await?;
    Ok(Json(animal))
}
