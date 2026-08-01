pub mod animals;
pub mod backup;
pub mod health;
pub mod removal;
pub mod reports;
pub mod sessions;
pub mod sync;
pub mod tags;

use axum::{
    routing::{delete, get, patch, post},
    Router,
};
use sqlx::SqlitePool;

pub fn router(pool: SqlitePool) -> Router {
    Router::new()
        // Tags
        .route("/api/v1/tags", get(tags::list_tags).post(tags::create_tag))
        .route(
            "/api/v1/tags/:tag_number",
            get(tags::get_tag_by_number).delete(tags::delete_tag),
        )
        // Animals
        .route(
            "/api/v1/animals",
            get(animals::list_animals).post(animals::create_animal),
        )
        .route(
            "/api/v1/animals/:id",
            get(animals::get_animal)
                .patch(animals::patch_animal)
                .delete(animals::delete_animal),
        )
        // Vaccinations
        .route(
            "/api/v1/animals/:animal_id/vaccinations",
            get(health::list_vaccinations).post(health::create_vaccination),
        )
        .route(
            "/api/v1/animals/:animal_id/vaccinations/:id",
            patch(health::patch_vaccination).delete(health::delete_vaccination),
        )
        // Pregnancies
        .route(
            "/api/v1/animals/:animal_id/pregnancies",
            get(health::list_pregnancies).post(health::create_pregnancy),
        )
        .route(
            "/api/v1/animals/:animal_id/pregnancies/:id",
            patch(health::patch_pregnancy).delete(health::delete_pregnancy),
        )
        // Tests
        .route(
            "/api/v1/animals/:animal_id/tests",
            get(health::list_tests).post(health::create_test),
        )
        .route(
            "/api/v1/animals/:animal_id/tests/:id",
            patch(health::patch_test).delete(health::delete_test),
        )
        // Weights
        .route(
            "/api/v1/animals/:animal_id/weights",
            get(health::list_weights).post(health::create_weight),
        )
        .route(
            "/api/v1/animals/:animal_id/weights/:id",
            patch(health::patch_weight).delete(health::delete_weight),
        )
        // Removal
        .route(
            "/api/v1/animals/:animal_id/removal",
            post(removal::create_removal)
                .patch(removal::patch_removal)
                .delete(removal::delete_removal),
        )
        // Sync
        .route(
            "/api/v1/sync/scans",
            post(sync::sync_scans).get(sync::list_scan_events),
        )
        // Sessions
        .route(
            "/api/v1/sessions/sync",
            post(sessions::sync_session),
        )
        .route(
            "/api/v1/sessions",
            get(sessions::list_sessions),
        )
        .route(
            "/api/v1/sessions/:id",
            get(sessions::get_session)
                .patch(sessions::patch_session)
                .delete(sessions::delete_session),
        )
        .route(
            "/api/v1/sessions/:id/audio",
            get(sessions::get_session_note_audio),
        )
        .route(
            "/api/v1/sessions/:id/records/:eid/audio",
            get(sessions::get_record_audio),
        )
        // Reports
        .route("/api/v1/reports/herd", get(reports::herd_report))
        // Backup / Restore
        .route(
            "/api/v1/backup",
            get(backup::backup).post(backup::restore),
        )
        .with_state(pool)
}
