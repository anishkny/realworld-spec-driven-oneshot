use crate::auth::{create_token, hash_password, verify_password, verify_token};
use crate::models::*;
use crate::state::AppState;
use axum::{
    async_trait,
    extract::{Path, Query, State, FromRequest, Request},
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    Json,
};
use serde::Deserialize;
use sqlx::{PgPool, Row};
use chrono::{DateTime, Utc};

pub struct JsonExtractor<T>(pub T);

#[async_trait]
impl<S, T> FromRequest<S> for JsonExtractor<T>
where
    T: serde::de::DeserializeOwned,
    S: Send + Sync,
{
    type Rejection = Response;

    async fn from_request(req: Request, state: &S) -> Result<Self, Self::Rejection> {
        match Json::<T>::from_request(req, state).await {
            Ok(Json(value)) => Ok(JsonExtractor(value)),
            Err(_) => Err((
                StatusCode::UNPROCESSABLE_ENTITY,
                Json(ErrorResponse {
                    errors: ErrorBody {
                        body: vec!["invalid request body".to_string()],
                    },
                }),
            )
                .into_response()),
        }
    }
}

pub async fn health_check() -> impl IntoResponse {
    StatusCode::OK
}

fn get_user_id_from_headers(headers: &HeaderMap) -> Result<i32, Response> {
    let auth_header = headers
        .get("authorization")
        .ok_or_else(|| StatusCode::UNAUTHORIZED.into_response())?
        .to_str()
        .map_err(|_| StatusCode::UNAUTHORIZED.into_response())?;

    let claims = verify_token(auth_header).map_err(|_| StatusCode::UNAUTHORIZED.into_response())?;
    claims
        .sub
        .parse::<i32>()
        .map_err(|_| StatusCode::UNAUTHORIZED.into_response())
}

fn get_optional_user_id(headers: &HeaderMap) -> Option<i32> {
    headers
        .get("authorization")
        .and_then(|h| h.to_str().ok())
        .and_then(|token| verify_token(token).ok())
        .and_then(|claims| claims.sub.parse::<i32>().ok())
}

fn is_valid_email(email: &str) -> bool {
    email.contains('@') && email.contains('.') && email.len() > 3
}

pub async fn register_user(
    State(state): State<AppState>,
    JsonExtractor(payload): JsonExtractor<RegisterUserRequest>,
) -> Result<Json<UserResponse>, Response> {
    if !is_valid_email(&payload.user.email) {
        return Err((
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(ErrorResponse {
                errors: ErrorBody {
                    body: vec!["invalid email format".to_string()],
                },
            }),
        )
            .into_response());
    }

    let password_hash = hash_password(&payload.user.password)
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let row = sqlx::query(
        "INSERT INTO users (username, email, password_hash) VALUES ($1, $2, $3) RETURNING id, username, email, bio, image, created_at, updated_at"
    )
    .bind(&payload.user.username)
    .bind(&payload.user.email)
    .bind(&password_hash)
    .fetch_one(&state.pool)
    .await
    .map_err(|_| {
        (
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(ErrorResponse {
                errors: ErrorBody {
                    body: vec!["user already exists".to_string()],
                },
            }),
        )
            .into_response()
    })?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let email: String = row.get(2);
    let bio: Option<String> = row.get(3);
    let image: Option<String> = row.get(4);
    let created_at: DateTime<Utc> = row.get(5);
    let updated_at: DateTime<Utc> = row.get(6);

    let token = create_token(&id.to_string())
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(Json(UserResponse {
        user: UserData {
            email,
            token,
            username,
            bio,
            image,
            created_at: Some(created_at),
            updated_at: Some(updated_at),
        },
    }))
}

pub async fn login_user(
    State(state): State<AppState>,
    JsonExtractor(payload): JsonExtractor<LoginUserRequest>,
) -> Result<Json<UserResponse>, Response> {
    let row = sqlx::query(
        "SELECT id, username, email, password_hash, bio, image, created_at, updated_at FROM users WHERE email = $1"
    )
    .bind(&payload.user.email)
    .fetch_optional(&state.pool)
    .await
    .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
    .ok_or_else(|| StatusCode::UNAUTHORIZED.into_response())?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let email: String = row.get(2);
    let password_hash: String = row.get(3);
    let bio: Option<String> = row.get(4);
    let image: Option<String> = row.get(5);
    let created_at: DateTime<Utc> = row.get(6);
    let updated_at: DateTime<Utc> = row.get(7);

    let password_valid = verify_password(&payload.user.password, &password_hash)
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    if !password_valid {
        return Err(StatusCode::UNAUTHORIZED.into_response());
    }

    let token = create_token(&id.to_string())
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(Json(UserResponse {
        user: UserData {
            email,
            token,
            username,
            bio,
            image,
            created_at: Some(created_at),
            updated_at: Some(updated_at),
        },
    }))
}

pub async fn get_current_user(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<UserResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id, username, email, bio, image, created_at, updated_at FROM users WHERE id = $1")
        .bind(user_id)
        .fetch_one(&state.pool)
        .await
        .map_err(|_| StatusCode::NOT_FOUND.into_response())?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let email: String = row.get(2);
    let bio: Option<String> = row.get(3);
    let image: Option<String> = row.get(4);
    let created_at: DateTime<Utc> = row.get(5);
    let updated_at: DateTime<Utc> = row.get(6);

    let token = create_token(&id.to_string())
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(Json(UserResponse {
        user: UserData {
            email,
            token,
            username,
            bio,
            image,
            created_at: Some(created_at),
            updated_at: Some(updated_at),
        },
    }))
}

pub async fn update_user(
    State(state): State<AppState>,
    headers: HeaderMap,
    JsonExtractor(payload): JsonExtractor<UpdateUserRequest>,
) -> Result<Json<UserResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let mut updates = vec!["updated_at = CURRENT_TIMESTAMP".to_string()];
    let mut bind_values: Vec<String> = vec![];
    let mut param_num = 1;

    let email_val;
    let username_val;
    let password_val;
    let bio_val;
    let image_val;

    if let Some(email) = &payload.user.email {
        if !is_valid_email(email) {
            return Err((
                StatusCode::UNPROCESSABLE_ENTITY,
                Json(ErrorResponse {
                    errors: ErrorBody {
                        body: vec!["invalid email format".to_string()],
                    },
                }),
            )
                .into_response());
        }
        email_val = email.clone();
        updates.push(format!("email = ${}", param_num));
        bind_values.push(email_val);
        param_num += 1;
    }
    if let Some(username) = &payload.user.username {
        username_val = username.clone();
        updates.push(format!("username = ${}", param_num));
        bind_values.push(username_val);
        param_num += 1;
    }
    if let Some(password) = &payload.user.password {
        let hash = hash_password(password)
            .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;
        password_val = hash;
        updates.push(format!("password_hash = ${}", param_num));
        bind_values.push(password_val);
        param_num += 1;
    }
    if let Some(bio) = &payload.user.bio {
        bio_val = bio.clone();
        updates.push(format!("bio = ${}", param_num));
        bind_values.push(bio_val);
        param_num += 1;
    }
    if let Some(image) = &payload.user.image {
        image_val = image.clone();
        updates.push(format!("image = ${}", param_num));
        bind_values.push(image_val);
        param_num += 1;
    }

    let query_str = format!(
        "UPDATE users SET {} WHERE id = ${} RETURNING id, username, email, bio, image, created_at, updated_at",
        updates.join(", "),
        param_num
    );

    let mut query = sqlx::query(&query_str);
    for value in &bind_values {
        query = query.bind(value);
    }
    query = query.bind(user_id);

    let row = query
        .fetch_one(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let email: String = row.get(2);
    let bio: Option<String> = row.get(3);
    let image: Option<String> = row.get(4);
    let created_at: DateTime<Utc> = row.get(5);
    let updated_at: DateTime<Utc> = row.get(6);

    let token = create_token(&id.to_string())
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(Json(UserResponse {
        user: UserData {
            email,
            token,
            username,
            bio,
            image,
            created_at: Some(created_at),
            updated_at: Some(updated_at),
        },
    }))
}

pub async fn get_profile(
    State(state): State<AppState>,
    Path(username): Path<String>,
    headers: HeaderMap,
) -> Result<Json<ProfileResponse>, Response> {
    let current_user_id = get_optional_user_id(&headers);

    let row = sqlx::query("SELECT id, username, bio, image FROM users WHERE username = $1")
        .bind(&username)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let bio: Option<String> = row.get(2);
    let image: Option<String> = row.get(3);

    let following = if let Some(current_id) = current_user_id {
        sqlx::query("SELECT 1 FROM follows WHERE follower_id = $1 AND followee_id = $2")
            .bind(current_id)
            .bind(id)
            .fetch_optional(&state.pool)
            .await
            .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
            .is_some()
    } else {
        false
    };

    Ok(Json(ProfileResponse {
        profile: Profile {
            username,
            bio,
            image,
            following,
        },
    }))
}

pub async fn follow_user(
    State(state): State<AppState>,
    Path(username): Path<String>,
    headers: HeaderMap,
) -> Result<Json<ProfileResponse>, Response> {
    let current_user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id, username, bio, image FROM users WHERE username = $1")
        .bind(&username)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let bio: Option<String> = row.get(2);
    let image: Option<String> = row.get(3);

    sqlx::query("INSERT INTO follows (follower_id, followee_id) VALUES ($1, $2) ON CONFLICT DO NOTHING")
        .bind(current_user_id)
        .bind(id)
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(Json(ProfileResponse {
        profile: Profile {
            username,
            bio,
            image,
            following: true,
        },
    }))
}

pub async fn unfollow_user(
    State(state): State<AppState>,
    Path(username): Path<String>,
    headers: HeaderMap,
) -> Result<Json<ProfileResponse>, Response> {
    let current_user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id, username, bio, image FROM users WHERE username = $1")
        .bind(&username)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let id: i32 = row.get(0);
    let username: String = row.get(1);
    let bio: Option<String> = row.get(2);
    let image: Option<String> = row.get(3);

    sqlx::query("DELETE FROM follows WHERE follower_id = $1 AND followee_id = $2")
        .bind(current_user_id)
        .bind(id)
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(Json(ProfileResponse {
        profile: Profile {
            username,
            bio,
            image,
            following: false,
        },
    }))
}

fn generate_slug(title: &str) -> String {
    let base_slug = slug::slugify(title);
    let random_suffix = uuid::Uuid::new_v4()
        .to_string()
        .chars()
        .take(6)
        .collect::<String>();
    format!("{}-{}", base_slug, random_suffix)
}

async fn get_article_with_details(
    pool: &PgPool,
    article_id: i32,
    current_user_id: Option<i32>,
) -> Result<Article, Response> {
    let row = sqlx::query(
        "SELECT a.id, a.slug, a.title, a.description, a.body, a.created_at, a.updated_at, a.author_id, u.username, u.bio, u.image FROM articles a JOIN users u ON a.author_id = u.id WHERE a.id = $1"
    )
    .bind(article_id)
    .fetch_one(pool)
    .await
    .map_err(|_| StatusCode::NOT_FOUND.into_response())?;

    let slug: String = row.get(1);
    let title: String = row.get(2);
    let description: String = row.get(3);
    let body: String = row.get(4);
    let created_at: DateTime<Utc> = row.get(5);
    let updated_at: DateTime<Utc> = row.get(6);
    let author_id: i32 = row.get(7);
    let author_username: String = row.get(8);
    let author_bio: Option<String> = row.get(9);
    let author_image: Option<String> = row.get(10);

    let tag_rows = sqlx::query("SELECT t.name FROM tags t JOIN article_tags at ON t.id = at.tag_id WHERE at.article_id = $1")
        .bind(article_id)
        .fetch_all(pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let tags: Vec<String> = tag_rows.iter().map(|r| r.get(0)).collect();

    let favorited = if let Some(user_id) = current_user_id {
        sqlx::query("SELECT 1 FROM favorites WHERE user_id = $1 AND article_id = $2")
            .bind(user_id)
            .bind(article_id)
            .fetch_optional(pool)
            .await
            .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
            .is_some()
    } else {
        false
    };

    let count_row = sqlx::query("SELECT COUNT(*) FROM favorites WHERE article_id = $1")
        .bind(article_id)
        .fetch_one(pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;
    let favorites_count: i64 = count_row.get(0);

    let following = if let Some(user_id) = current_user_id {
        sqlx::query("SELECT 1 FROM follows WHERE follower_id = $1 AND followee_id = $2")
            .bind(user_id)
            .bind(author_id)
            .fetch_optional(pool)
            .await
            .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
            .is_some()
    } else {
        false
    };

    Ok(Article {
        slug,
        title,
        description,
        body,
        tag_list: tags,
        created_at: created_at,
        updated_at: updated_at,
        favorited,
        favorites_count,
        author: Profile {
            username: author_username,
            bio: author_bio,
            image: author_image,
            following,
        },
    })
}

pub async fn create_article(
    State(state): State<AppState>,
    headers: HeaderMap,
    JsonExtractor(payload): JsonExtractor<CreateArticleRequest>,
) -> Result<Json<ArticleResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let slug = generate_slug(&payload.article.title);

    let row = sqlx::query("INSERT INTO articles (slug, title, description, body, author_id) VALUES ($1, $2, $3, $4, $5) RETURNING id")
        .bind(&slug)
        .bind(&payload.article.title)
        .bind(&payload.article.description)
        .bind(&payload.article.body)
        .bind(user_id)
        .fetch_one(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let article_id: i32 = row.get(0);

    for tag_name in &payload.article.tagList {
        let tag_row = sqlx::query("INSERT INTO tags (name) VALUES ($1) ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name RETURNING id")
            .bind(tag_name)
            .fetch_one(&state.pool)
            .await
            .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

        let tag_id: i32 = tag_row.get(0);

        sqlx::query("INSERT INTO article_tags (article_id, tag_id) VALUES ($1, $2)")
            .bind(article_id)
            .bind(tag_id)
            .execute(&state.pool)
            .await
            .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;
    }

    let article_data = get_article_with_details(&state.pool, article_id, Some(user_id)).await?;

    Ok(Json(ArticleResponse {
        article: article_data,
    }))
}

pub async fn get_article(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
) -> Result<Json<ArticleResponse>, Response> {
    let current_user_id = get_optional_user_id(&headers);

    let row = sqlx::query("SELECT id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);

    let article_data = get_article_with_details(&state.pool, article_id, current_user_id).await?;

    Ok(Json(ArticleResponse {
        article: article_data,
    }))
}

pub async fn update_article(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
    JsonExtractor(payload): JsonExtractor<UpdateArticleRequest>,
) -> Result<Json<ArticleResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id, author_id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);
    let author_id: i32 = row.get(1);

    if author_id != user_id {
        return Err(StatusCode::FORBIDDEN.into_response());
    }

    let new_slug = if let Some(title) = &payload.article.title {
        Some(generate_slug(title))
    } else {
        None
    };

    let mut updates = vec!["updated_at = CURRENT_TIMESTAMP".to_string()];
    let mut bind_values: Vec<String> = vec![];
    let mut param_num = 1;

    if let Some(title) = &payload.article.title {
        updates.push(format!("title = ${}", param_num));
        bind_values.push(title.clone());
        param_num += 1;
    }
    if let Some(description) = &payload.article.description {
        updates.push(format!("description = ${}", param_num));
        bind_values.push(description.clone());
        param_num += 1;
    }
    if let Some(body) = &payload.article.body {
        updates.push(format!("body = ${}", param_num));
        bind_values.push(body.clone());
        param_num += 1;
    }
    if let Some(ref s) = new_slug {
        updates.push(format!("slug = ${}", param_num));
        bind_values.push(s.clone());
        param_num += 1;
    }

    let query_str = format!(
        "UPDATE articles SET {} WHERE id = ${}",
        updates.join(", "),
        param_num
    );

    let mut query = sqlx::query(&query_str);
    for value in &bind_values {
        query = query.bind(value);
    }
    query = query.bind(article_id);

    query
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let article_data = get_article_with_details(&state.pool, article_id, Some(user_id)).await?;

    Ok(Json(ArticleResponse {
        article: article_data,
    }))
}

pub async fn delete_article(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
) -> Result<StatusCode, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id, author_id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);
    let author_id: i32 = row.get(1);

    if author_id != user_id {
        return Err(StatusCode::FORBIDDEN.into_response());
    }

    sqlx::query("DELETE FROM articles WHERE id = $1")
        .bind(article_id)
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(StatusCode::OK)
}

#[derive(Deserialize)]
pub struct ListArticlesQuery {
    tag: Option<String>,
    author: Option<String>,
    favorited: Option<String>,
    limit: Option<i64>,
    offset: Option<i64>,
}

pub async fn list_articles(
    State(state): State<AppState>,
    Query(params): Query<ListArticlesQuery>,
    headers: HeaderMap,
) -> Result<Json<ArticlesResponse>, Response> {
    let current_user_id = get_optional_user_id(&headers);
    let limit = params.limit.unwrap_or(20);
    let offset = params.offset.unwrap_or(0);

    let mut conditions = vec![];
    
    if let Some(tag) = &params.tag {
        conditions.push(format!(
            "EXISTS (SELECT 1 FROM article_tags at JOIN tags t ON at.tag_id = t.id WHERE at.article_id = a.id AND t.name = '{}')",
            tag.replace("'", "''")
        ));
    }
    
    if let Some(author) = &params.author {
        conditions.push(format!("u.username = '{}'", author.replace("'", "''")));
    }
    
    if let Some(favorited) = &params.favorited {
        conditions.push(format!(
            "EXISTS (SELECT 1 FROM favorites f JOIN users fu ON f.user_id = fu.id WHERE f.article_id = a.id AND fu.username = '{}')",
            favorited.replace("'", "''")
        ));
    }

    let where_clause = if conditions.is_empty() {
        String::new()
    } else {
        format!("WHERE {}", conditions.join(" AND "))
    };

    let query = format!(
        "SELECT a.id FROM articles a JOIN users u ON a.author_id = u.id {} ORDER BY a.created_at DESC LIMIT {} OFFSET {}",
        where_clause, limit, offset
    );

    let article_rows = sqlx::query(&query)
        .fetch_all(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let mut articles = vec![];
    for row in article_rows {
        let article_id: i32 = row.get(0);
        let article = get_article_with_details(&state.pool, article_id, current_user_id).await?;
        articles.push(article);
    }

    let articles_count = articles.len();

    Ok(Json(ArticlesResponse {
        articles,
        articles_count,
    }))
}

pub async fn feed_articles(
    State(state): State<AppState>,
    Query(params): Query<ListArticlesQuery>,
    headers: HeaderMap,
) -> Result<Json<ArticlesResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;
    let limit = params.limit.unwrap_or(20);
    let offset = params.offset.unwrap_or(0);

    let article_rows = sqlx::query(
        "SELECT a.id FROM articles a WHERE a.author_id IN (SELECT followee_id FROM follows WHERE follower_id = $1) ORDER BY a.created_at DESC LIMIT $2 OFFSET $3"
    )
    .bind(user_id)
    .bind(limit)
    .bind(offset)
    .fetch_all(&state.pool)
    .await
    .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let mut articles = vec![];
    for row in article_rows {
        let article_id: i32 = row.get(0);
        let article = get_article_with_details(&state.pool, article_id, Some(user_id)).await?;
        articles.push(article);
    }

    let articles_count = articles.len();

    Ok(Json(ArticlesResponse {
        articles,
        articles_count,
    }))
}

pub async fn favorite_article(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
) -> Result<Json<ArticleResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);

    sqlx::query("INSERT INTO favorites (user_id, article_id) VALUES ($1, $2) ON CONFLICT DO NOTHING")
        .bind(user_id)
        .bind(article_id)
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let article_data = get_article_with_details(&state.pool, article_id, Some(user_id)).await?;

    Ok(Json(ArticleResponse {
        article: article_data,
    }))
}

pub async fn unfavorite_article(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
) -> Result<Json<ArticleResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);

    sqlx::query("DELETE FROM favorites WHERE user_id = $1 AND article_id = $2")
        .bind(user_id)
        .bind(article_id)
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let article_data = get_article_with_details(&state.pool, article_id, Some(user_id)).await?;

    Ok(Json(ArticleResponse {
        article: article_data,
    }))
}

pub async fn add_comment(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
    JsonExtractor(payload): JsonExtractor<CreateCommentRequest>,
) -> Result<Json<CommentResponse>, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);

    let comment_row = sqlx::query("INSERT INTO comments (body, article_id, author_id) VALUES ($1, $2, $3) RETURNING id, body, created_at, updated_at")
        .bind(&payload.comment.body)
        .bind(article_id)
        .bind(user_id)
        .fetch_one(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let comment_id: i32 = comment_row.get(0);
    let body: String = comment_row.get(1);
    let created_at: DateTime<Utc> = comment_row.get(2);
    let updated_at: DateTime<Utc> = comment_row.get(3);

    let author_row = sqlx::query("SELECT username, bio, image FROM users WHERE id = $1")
        .bind(user_id)
        .fetch_one(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let author_username: String = author_row.get(0);
    let author_bio: Option<String> = author_row.get(1);
    let author_image: Option<String> = author_row.get(2);

    Ok(Json(CommentResponse {
        comment: Comment {
            id: comment_id as i64,
            created_at: created_at,
            updated_at: updated_at,
            body,
            author: Profile {
                username: author_username,
                bio: author_bio,
                image: author_image,
                following: false,
            },
        },
    }))
}

pub async fn get_comments(
    State(state): State<AppState>,
    Path(slug): Path<String>,
    headers: HeaderMap,
) -> Result<Json<CommentsResponse>, Response> {
    let current_user_id = get_optional_user_id(&headers);

    let row = sqlx::query("SELECT id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);

    let comment_rows = sqlx::query(
        "SELECT c.id, c.body, c.created_at, c.updated_at, c.author_id, u.username, u.bio, u.image FROM comments c JOIN users u ON c.author_id = u.id WHERE c.article_id = $1 ORDER BY c.created_at DESC"
    )
    .bind(article_id)
    .fetch_all(&state.pool)
    .await
    .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let mut comments = vec![];
    for comment_row in comment_rows {
        let comment_id: i32 = comment_row.get(0);
        let body: String = comment_row.get(1);
        let created_at: DateTime<Utc> = comment_row.get(2);
        let updated_at: DateTime<Utc> = comment_row.get(3);
        let author_id: i32 = comment_row.get(4);
        let author_username: String = comment_row.get(5);
        let author_bio: Option<String> = comment_row.get(6);
        let author_image: Option<String> = comment_row.get(7);

        let following = if let Some(user_id) = current_user_id {
            sqlx::query("SELECT 1 FROM follows WHERE follower_id = $1 AND followee_id = $2")
                .bind(user_id)
                .bind(author_id)
                .fetch_optional(&state.pool)
                .await
                .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
                .is_some()
        } else {
            false
        };

        comments.push(Comment {
            id: comment_id as i64,
            created_at: created_at,
            updated_at: updated_at,
            body,
            author: Profile {
                username: author_username,
                bio: author_bio,
                image: author_image,
                following,
            },
        });
    }

    Ok(Json(CommentsResponse { comments }))
}

pub async fn delete_comment(
    State(state): State<AppState>,
    Path((slug, comment_id)): Path<(String, i64)>,
    headers: HeaderMap,
) -> Result<StatusCode, Response> {
    let user_id = get_user_id_from_headers(&headers)?;

    let row = sqlx::query("SELECT id FROM articles WHERE slug = $1")
        .bind(&slug)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let article_id: i32 = row.get(0);

    let comment_row = sqlx::query("SELECT author_id FROM comments WHERE id = $1 AND article_id = $2")
        .bind(comment_id as i32)
        .bind(article_id)
        .fetch_optional(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?
        .ok_or_else(|| StatusCode::NOT_FOUND.into_response())?;

    let author_id: i32 = comment_row.get(0);

    if author_id != user_id {
        return Err(StatusCode::FORBIDDEN.into_response());
    }

    sqlx::query("DELETE FROM comments WHERE id = $1")
        .bind(comment_id as i32)
        .execute(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    Ok(StatusCode::OK)
}

pub async fn get_tags(State(state): State<AppState>) -> Result<Json<TagsResponse>, Response> {
    let tag_rows = sqlx::query("SELECT name FROM tags ORDER BY name")
        .fetch_all(&state.pool)
        .await
        .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR).into_response())?;

    let tags: Vec<String> = tag_rows.iter().map(|r| r.get(0)).collect();

    Ok(Json(TagsResponse { tags }))
}
