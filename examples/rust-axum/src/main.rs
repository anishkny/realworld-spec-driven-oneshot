mod models;
mod db;
mod handlers;
mod auth;
mod state;

use axum::{
    routing::{get, post, put, delete},
    Router,
};
use sqlx::postgres::PgPoolOptions;
use std::env;
use tower_http::cors::CorsLayer;

#[tokio::main]
async fn main() {
    let postgres_uri = env::var("POSTGRES_URI")
        .unwrap_or_else(|_| "postgres://postgres:password@localhost:5432/postgres".to_string());

    let pool = PgPoolOptions::new()
        .max_connections(5)
        .connect(&postgres_uri)
        .await
        .expect("Failed to connect to Postgres");

    db::init_db(&pool).await.expect("Failed to initialize database");

    let app_state = state::AppState { pool };

    let app = Router::new()
        .route("/", get(handlers::health_check))
        .route("/api/users", post(handlers::register_user))
        .route("/api/users/login", post(handlers::login_user))
        .route("/api/user", get(handlers::get_current_user))
        .route("/api/user", put(handlers::update_user))
        .route("/api/profiles/:username", get(handlers::get_profile))
        .route("/api/profiles/:username/follow", post(handlers::follow_user))
        .route("/api/profiles/:username/follow", delete(handlers::unfollow_user))
        .route("/api/articles", get(handlers::list_articles))
        .route("/api/articles", post(handlers::create_article))
        .route("/api/articles/feed", get(handlers::feed_articles))
        .route("/api/articles/:slug", get(handlers::get_article))
        .route("/api/articles/:slug", put(handlers::update_article))
        .route("/api/articles/:slug", delete(handlers::delete_article))
        .route("/api/articles/:slug/comments", get(handlers::get_comments))
        .route("/api/articles/:slug/comments", post(handlers::add_comment))
        .route("/api/articles/:slug/comments/:id", delete(handlers::delete_comment))
        .route("/api/articles/:slug/favorite", post(handlers::favorite_article))
        .route("/api/articles/:slug/favorite", delete(handlers::unfavorite_article))
        .route("/api/tags", get(handlers::get_tags))
        .layer(CorsLayer::permissive())
        .with_state(app_state);

    let listener = tokio::net::TcpListener::bind("0.0.0.0:3000")
        .await
        .expect("Failed to bind to port 3000");

    println!("Server running on http://0.0.0.0:3000");

    axum::serve(listener, app)
        .await
        .expect("Server failed");
}
