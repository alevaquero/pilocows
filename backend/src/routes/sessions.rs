use axum::{
    extract::{Path, State},
    http::{header, StatusCode},
    response::IntoResponse,
    Json,
};
use base64::{engine::general_purpose::STANDARD as BASE64, Engine};
use sqlx::SqlitePool;

use crate::{
    error::{AppError, Result},
    models::{
        IncomingSessionRecord, PatchSession, Session, SessionDetail, SessionRecord,
        SessionSummary, SyncSessionPayload, SyncSessionResponse,
    },
};

/// Decode an optional base64 field from a sync payload. `None` means "no
/// audio to send" (old handheld, or nothing changed) — not an error. Only a
/// present-but-invalid string is a bad request.
fn decode_b64_audio(field: &Option<String>) -> Result<Option<Vec<u8>>> {
    match field {
        None => Ok(None),
        Some(s) if s.is_empty() => Ok(None),
        Some(s) => BASE64
            .decode(s)
            .map(Some)
            .map_err(|e| AppError::BadRequest(format!("invalid base64 audio: {e}"))),
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Convert a Unix timestamp (i64 seconds) to an ISO-8601 UTC string.
fn unix_to_iso(ts: i64) -> String {
    // SQLite datetime() accepts Unix timestamps via 'unixepoch' modifier.
    // We compute it in Rust to keep the conversion self-contained.
    let secs = ts.max(0);
    let s = secs % 60;
    let m = (secs / 60) % 60;
    let h = (secs / 3600) % 24;
    let days = secs / 86400;
    // Simple Gregorian calendar computation (good for 2000–2099)
    let z = days + 719468;
    let era = z / 146097;
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let year = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = if month <= 2 { year + 1 } else { year };
    format!("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", year, month, day, h, m, s)
}

/// Build a compact JSON blob from the type-specific fields of a scan record.
fn build_event_data(rec: &IncomingSessionRecord) -> String {
    match rec.session_type {
        1 => {
            // Weighing
            if let Some(w) = rec.weight_kg {
                format!("{{\"weight_kg\":{}}}", w)
            } else {
                "{}".to_string()
            }
        }
        2 => {
            // Vaccination
            if let Some(v) = &rec.vaccines {
                let escaped = v.replace('"', "\\\"");
                format!("{{\"vaccines\":\"{}\"}}", escaped)
            } else {
                "{}".to_string()
            }
        }
        3 => {
            // Pregnancy
            if let Some(p) = &rec.pregnancy {
                let escaped = p.replace('"', "\\\"");
                format!("{{\"pregnancy\":\"{}\"}}", escaped)
            } else {
                "{}".to_string()
            }
        }
        4 => {
            // Test
            match (&rec.test_result, &rec.test_name) {
                (Some(result), Some(name)) => {
                    let r = result.replace('"', "\\\"");
                    let n = name.replace('"', "\\\"");
                    format!("{{\"test_result\":\"{}\",\"test_name\":\"{}\"}}", r, n)
                }
                (Some(result), None) => {
                    let r = result.replace('"', "\\\"");
                    format!("{{\"test_result\":\"{}\"}}", r)
                }
                _ => "{}".to_string(),
            }
        }
        _ => "{}".to_string(),
    }
}

// ─── POST /api/v1/sessions/sync ───────────────────────────────────────────────

pub async fn sync_session(
    State(pool): State<SqlitePool>,
    Json(payload): Json<SyncSessionPayload>,
) -> Result<(StatusCode, Json<SyncSessionResponse>)> {
    let s = &payload.session;
    let created_at = unix_to_iso(s.created_at);
    let note_audio = decode_b64_audio(&s.note_audio_b64)?;

    // Upsert session row — preserve frontend-editable fields on conflict.
    // note_audio only overwrites when the payload actually carries bytes
    // (COALESCE onto the existing column) — old handhelds and unchanged
    // re-syncs send no audio at all, and that must not wipe out audio a
    // previous sync already stored.
    let session_id: i64 = sqlx::query_scalar(
        "INSERT INTO sessions
             (handheld_session_id, device_id, name, type,
              created_at, tag_count, handheld_note, note_audio, synced_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
         ON CONFLICT(handheld_session_id, device_id) DO UPDATE SET
             name          = excluded.name,
             tag_count     = excluded.tag_count,
             handheld_note = excluded.handheld_note,
             note_audio    = COALESCE(excluded.note_audio, sessions.note_audio),
             synced_at     = strftime('%Y-%m-%dT%H:%M:%SZ', 'now')
         RETURNING id",
    )
    .bind(s.handheld_session_id)
    .bind(&s.device_id)
    .bind(&s.name)
    .bind(s.session_type)
    .bind(&created_at)
    .bind(s.tag_count)
    .bind(&s.note)
    .bind(&note_audio)
    .fetch_one(&pool)
    .await?;

    // Upsert each tag record.
    let mut upserted_records = 0usize;
    for rec in &payload.records {
        let scanned_at = unix_to_iso(rec.ts);
        let event_data = build_event_data(rec);
        let audio = decode_b64_audio(&rec.audio_b64)?;

        sqlx::query(
            "INSERT INTO session_records (session_id, eid, scanned_at, event_data, note, audio)
             VALUES (?, ?, ?, ?, ?, ?)
             ON CONFLICT(session_id, eid) DO UPDATE SET
                 scanned_at = excluded.scanned_at,
                 event_data = excluded.event_data,
                 note       = excluded.note,
                 audio      = COALESCE(excluded.audio, session_records.audio)",
        )
        .bind(session_id)
        .bind(&rec.eid)
        .bind(&scanned_at)
        .bind(&event_data)
        .bind(&rec.note)
        .bind(&audio)
        .execute(&pool)
        .await?;

        upserted_records += 1;
    }

    Ok((
        StatusCode::OK,
        Json(SyncSessionResponse {
            session_id,
            upserted_records,
        }),
    ))
}

// ─── GET /api/v1/sessions ─────────────────────────────────────────────────────

pub async fn list_sessions(
    State(pool): State<SqlitePool>,
) -> Result<Json<Vec<SessionSummary>>> {
    let rows = sqlx::query_as::<_, SessionSummary>(
        "SELECT s.id, s.handheld_session_id, s.device_id, s.name, s.type,
                s.created_at, s.tag_count, s.handheld_note, s.farm, s.operator,
                s.comments, s.synced_at,
                (s.note_audio IS NOT NULL) AS has_note_audio,
                COUNT(r.id) AS record_count
         FROM sessions s
         LEFT JOIN session_records r ON r.session_id = s.id
         GROUP BY s.id
         ORDER BY s.created_at DESC",
    )
    .fetch_all(&pool)
    .await?;
    Ok(Json(rows))
}

// ─── GET /api/v1/sessions/:id ─────────────────────────────────────────────────

pub async fn get_session(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
) -> Result<Json<SessionDetail>> {
    let session = sqlx::query_as::<_, Session>(
        "SELECT id, handheld_session_id, device_id, name, type, created_at,
                tag_count, handheld_note, farm, operator, comments, synced_at,
                (note_audio IS NOT NULL) AS has_note_audio
         FROM sessions WHERE id = ?",
    )
    .bind(id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    // Fetch records, joining tags/animals to get registration state.
    let records = sqlx::query_as::<_, SessionRecord>(
        "SELECT r.id, r.session_id, r.eid, r.scanned_at, r.event_data, r.note,
                (r.audio IS NOT NULL) AS has_audio,
                CASE WHEN t.id IS NOT NULL THEN 1 ELSE 0 END AS tag_registered,
                a.id AS animal_id
         FROM session_records r
         LEFT JOIN tags    t ON t.tag_number = r.eid
         LEFT JOIN animals a ON a.tag_id = t.id AND a.is_active = 1
         WHERE r.session_id = ?
         ORDER BY r.scanned_at ASC",
    )
    .bind(id)
    .fetch_all(&pool)
    .await?;

    Ok(Json(SessionDetail { session, records }))
}

// ─── GET /api/v1/sessions/:id/audio ────────────────────────────────────────────
// Session-level voice note, raw WAV bytes (not JSON) — kept separate from
// get_session so listing/detail fetches never have to pull the blob along
// with everything else.

pub async fn get_session_note_audio(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
) -> Result<impl IntoResponse> {
    let row: Option<Option<Vec<u8>>> =
        sqlx::query_scalar("SELECT note_audio FROM sessions WHERE id = ?")
            .bind(id)
            .fetch_optional(&pool)
            .await?;

    let bytes = row.flatten().ok_or(AppError::NotFound)?;
    Ok((StatusCode::OK, [(header::CONTENT_TYPE, "audio/wav")], bytes))
}

// ─── GET /api/v1/sessions/:id/records/:eid/audio ───────────────────────────────
// Per-tag voice note, raw WAV bytes.

pub async fn get_record_audio(
    State(pool): State<SqlitePool>,
    Path((id, eid)): Path<(i64, String)>,
) -> Result<impl IntoResponse> {
    let row: Option<Option<Vec<u8>>> = sqlx::query_scalar(
        "SELECT audio FROM session_records WHERE session_id = ? AND eid = ?",
    )
    .bind(id)
    .bind(&eid)
    .fetch_optional(&pool)
    .await?;

    let bytes = row.flatten().ok_or(AppError::NotFound)?;
    Ok((StatusCode::OK, [(header::CONTENT_TYPE, "audio/wav")], bytes))
}

// ─── PATCH /api/v1/sessions/:id ───────────────────────────────────────────────

pub async fn patch_session(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
    Json(body): Json<PatchSession>,
) -> Result<Json<Session>> {
    // Verify the session exists first.
    let existing = sqlx::query_as::<_, Session>(
        "SELECT id, handheld_session_id, device_id, name, type, created_at,
                tag_count, handheld_note, farm, operator, comments, synced_at,
                (note_audio IS NOT NULL) AS has_note_audio
         FROM sessions WHERE id = ?",
    )
    .bind(id)
    .fetch_optional(&pool)
    .await?
    .ok_or(AppError::NotFound)?;

    let farm          = body.farm.unwrap_or(existing.farm);
    let operator      = body.operator.unwrap_or(existing.operator);
    let comments      = body.comments.unwrap_or(existing.comments);
    let handheld_note = body.handheld_note.unwrap_or(existing.handheld_note);

    let updated = sqlx::query_as::<_, Session>(
        "UPDATE sessions
         SET farm = ?, operator = ?, comments = ?, handheld_note = ?
         WHERE id = ?
         RETURNING id, handheld_session_id, device_id, name, type, created_at,
                   tag_count, handheld_note, farm, operator, comments, synced_at,
                   (note_audio IS NOT NULL) AS has_note_audio",
    )
    .bind(&farm)
    .bind(&operator)
    .bind(&comments)
    .bind(&handheld_note)
    .bind(id)
    .fetch_one(&pool)
    .await?;

    Ok(Json(updated))
}

// ─── DELETE /api/v1/sessions/:id ──────────────────────────────────────────────

pub async fn delete_session(
    State(pool): State<SqlitePool>,
    Path(id): Path<i64>,
) -> Result<StatusCode> {
    let result = sqlx::query("DELETE FROM sessions WHERE id = ?")
        .bind(id)
        .execute(&pool)
        .await?;

    if result.rows_affected() == 0 {
        Err(AppError::NotFound)
    } else {
        Ok(StatusCode::NO_CONTENT)
    }
}
