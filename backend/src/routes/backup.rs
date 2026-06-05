use std::{
    path::PathBuf,
    sync::{Arc, Mutex},
};

use axum::{
    extract::{Extension, Multipart, State},
    http::{header, StatusCode},
    response::IntoResponse,
    Json,
};
use serde_json::json;
use sqlx::SqlitePool;
use tokio::sync::oneshot;

use crate::error::AppError;

pub type ShutdownTx = Arc<Mutex<Option<oneshot::Sender<()>>>>;

pub async fn backup(
    State(pool): State<SqlitePool>,
    Extension(db_path): Extension<Arc<PathBuf>>,
) -> Result<impl IntoResponse, AppError> {
    let timestamp = chrono::Local::now().format("%Y-%m-%d_%H-%M-%S").to_string();

    let db_dir = db_path.parent().unwrap_or_else(|| std::path::Path::new("."));
    let temp_path = db_dir.join(format!("pilocows_backup_{timestamp}.db"));
    let temp_str = temp_path.to_string_lossy().to_string();

    if temp_str.contains('\'') {
        return Err(AppError::BadRequest(
            "Database path contains invalid characters".to_string(),
        ));
    }

    sqlx::query(&format!("VACUUM INTO '{temp_str}'"))
        .execute(&pool)
        .await?;

    let bytes = tokio::fs::read(&temp_path).await.map_err(|e| {
        AppError::BadRequest(format!("Failed to read backup file: {e}"))
    })?;
    tokio::fs::remove_file(&temp_path).await.ok();

    let filename = format!("pilocows_backup_{timestamp}.db");
    let content_disposition = format!("attachment; filename=\"{filename}\"");

    Ok((
        [
            (header::CONTENT_TYPE, "application/octet-stream".to_string()),
            (header::CONTENT_DISPOSITION, content_disposition),
        ],
        bytes,
    ))
}

pub async fn restore(
    Extension(db_path): Extension<Arc<PathBuf>>,
    Extension(shutdown_tx): Extension<ShutdownTx>,
    mut multipart: Multipart,
) -> Result<impl IntoResponse, AppError> {
    const SQLITE_MAGIC: &[u8] = b"SQLite format 3\x00";

    while let Some(field) = multipart.next_field().await.map_err(|e| {
        AppError::BadRequest(format!("Multipart error: {e}"))
    })? {
        let data = field.bytes().await.map_err(|e| {
            AppError::BadRequest(format!("Failed to read field bytes: {e}"))
        })?;

        if !data.starts_with(SQLITE_MAGIC) {
            continue;
        }

        let pending_path = db_path
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .join("pilocows_pending.db");

        tokio::fs::write(&pending_path, &data).await.map_err(|e| {
            AppError::BadRequest(format!("Failed to write restore file: {e}"))
        })?;

        // Signal graceful shutdown after the response has been sent
        if let Ok(mut guard) = shutdown_tx.lock() {
            if let Some(tx) = guard.take() {
                tokio::spawn(async move {
                    tokio::time::sleep(std::time::Duration::from_millis(300)).await;
                    let _ = tx.send(());
                });
            }
        }

        return Ok((
            StatusCode::ACCEPTED,
            Json(json!({
                "status": "pending",
                "message": "Restore staged. Backend is restarting to apply the new database."
            })),
        ));
    }

    Err(AppError::BadRequest(
        "No valid SQLite database file found in the upload".to_string(),
    ))
}
