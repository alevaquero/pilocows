mod db;
mod error;
mod models;
mod routes;

use std::{
    path::PathBuf,
    sync::{Arc, Mutex},
};

use sqlx::migrate::MigrateDatabase;
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt, EnvFilter};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // Load .env if present (dev convenience)
    let _ = dotenvy::dotenv();

    // Tracing
    tracing_subscriber::registry()
        .with(EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()))
        .with(tracing_subscriber::fmt::layer())
        .init();

    tracing::info!("pilocows-backend v{}", env!("CARGO_PKG_VERSION"));

    // Config
    let database_url = std::env::var("DATABASE_URL")
        .unwrap_or_else(|_| "sqlite://pilocows.db".to_string());
    let port: u16 = std::env::var("PORT")
        .ok()
        .and_then(|p| p.parse().ok())
        .unwrap_or(8742);

    // Resolve the database file path (absolute)
    let db_file_str = database_url.trim_start_matches("sqlite://");
    let db_path: PathBuf = {
        let p = PathBuf::from(db_file_str);
        if p.is_absolute() {
            p
        } else {
            std::env::current_dir()?.join(p)
        }
    };

    // Apply any pending restore before opening the pool
    let pending_path = db_path.with_file_name("pilocows_pending.db");
    if pending_path.exists() {
        tracing::info!("Pending restore found — applying...");
        let wal = PathBuf::from(format!("{}-wal", db_path.display()));
        let shm = PathBuf::from(format!("{}-shm", db_path.display()));
        let _ = std::fs::remove_file(&wal);
        let _ = std::fs::remove_file(&shm);
        if db_path.exists() {
            let pre_restore = db_path.with_file_name("pilocows_pre_restore.db");
            std::fs::rename(&db_path, &pre_restore)?;
            tracing::info!("Previous DB saved as pilocows_pre_restore.db");
        }
        std::fs::rename(&pending_path, &db_path)?;
        tracing::info!("Restore applied successfully");
    }

    // Ensure the SQLite file exists before connecting
    if !sqlx::Sqlite::database_exists(&database_url).await? {
        tracing::info!("Creating database at {database_url}");
        sqlx::Sqlite::create_database(&database_url).await?;
    }

    // Connect pool
    let pool = db::connect(&database_url).await?;

    // Run migrations
    tracing::info!("Running migrations...");
    sqlx::migrate!("./migrations").run(&pool).await?;
    tracing::info!("Migrations complete");

    // Shutdown channel — used by the restore endpoint to trigger graceful exit
    let (shutdown_tx, shutdown_rx) = tokio::sync::oneshot::channel::<()>();
    let shutdown_tx: routes::backup::ShutdownTx = Arc::new(Mutex::new(Some(shutdown_tx)));

    // Build router
    let app = routes::router(pool)
        .layer(axum::Extension(Arc::new(db_path)))
        .layer(axum::Extension(shutdown_tx))
        .layer(tower_http::cors::CorsLayer::permissive());

    let addr = std::net::SocketAddr::from(([127, 0, 0, 1], port));
    tracing::info!("Listening on http://{addr}");

    let listener = tokio::net::TcpListener::bind(addr).await?;
    axum::serve(listener, app)
        .with_graceful_shutdown(async move {
            tokio::select! {
                _ = tokio::signal::ctrl_c() => {
                    tracing::info!("Received Ctrl+C, shutting down");
                }
                _ = shutdown_rx => {
                    tracing::info!("Restore staged, shutting down for restart");
                }
            }
        })
        .await?;

    Ok(())
}
