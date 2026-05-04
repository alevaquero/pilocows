mod db;
mod error;
mod models;
mod routes;

use std::net::SocketAddr;

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

    // Config
    let database_url = std::env::var("DATABASE_URL")
        .unwrap_or_else(|_| "sqlite://pilocows.db".to_string());
    let port: u16 = std::env::var("PORT")
        .ok()
        .and_then(|p| p.parse().ok())
        .unwrap_or(8742);

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

    // Build router
    let app = routes::router(pool).layer(
        tower_http::cors::CorsLayer::permissive(),
    );

    let addr = SocketAddr::from(([127, 0, 0, 1], port));
    tracing::info!("Listening on http://{addr}");

    let listener = tokio::net::TcpListener::bind(addr).await?;
    axum::serve(listener, app).await?;

    Ok(())
}
