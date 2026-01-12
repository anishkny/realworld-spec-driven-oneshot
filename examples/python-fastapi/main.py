import os
import re
from datetime import datetime, timezone
from typing import Optional, List
from contextlib import asynccontextmanager

from fastapi import FastAPI, Depends, HTTPException, Header
from fastapi.responses import Response
from pydantic import BaseModel, EmailStr, Field, validator
import psycopg2
from psycopg2.extras import RealDictCursor
import jwt
from passlib.hash import bcrypt
from email_validator import validate_email, EmailNotValidError

POSTGRES_URI = os.getenv("POSTGRES_URI", "postgres://postgres:password@localhost:5432/postgres")
JWT_SECRET = "secret-key-for-jwt"
JWT_ALGORITHM = "HS256"

db_conn = None

def get_db():
    return db_conn

def init_db():
    global db_conn
    db_conn = psycopg2.connect(POSTGRES_URI)
    cursor = db_conn.cursor()
    
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS users (
            id SERIAL PRIMARY KEY,
            username VARCHAR(255) UNIQUE NOT NULL,
            email VARCHAR(255) UNIQUE NOT NULL,
            password VARCHAR(255) NOT NULL,
            bio TEXT,
            image TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS articles (
            id SERIAL PRIMARY KEY,
            slug VARCHAR(255) UNIQUE NOT NULL,
            title VARCHAR(255) NOT NULL,
            description TEXT NOT NULL,
            body TEXT NOT NULL,
            author_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS article_tags (
            article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
            tag VARCHAR(255) NOT NULL,
            PRIMARY KEY (article_id, tag)
        )
    """)
    
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS comments (
            id SERIAL PRIMARY KEY,
            body TEXT NOT NULL,
            article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
            author_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS favorites (
            user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
            article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
            PRIMARY KEY (user_id, article_id)
        )
    """)
    
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS follows (
            follower_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
            followee_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
            PRIMARY KEY (follower_id, followee_id)
        )
    """)
    
    db_conn.commit()
    cursor.close()

@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    yield
    if db_conn:
        db_conn.close()

app = FastAPI(lifespan=lifespan)

# Models
class UserRegistration(BaseModel):
    username: str
    email: EmailStr
    password: str

class UserLogin(BaseModel):
    email: EmailStr
    password: str

class UserUpdate(BaseModel):
    email: Optional[EmailStr] = None
    username: Optional[str] = None
    password: Optional[str] = None
    bio: Optional[str] = None
    image: Optional[str] = None

class ArticleCreate(BaseModel):
    title: str
    description: str
    body: str
    tagList: Optional[List[str]] = []

class ArticleUpdate(BaseModel):
    title: Optional[str] = None
    description: Optional[str] = None
    body: Optional[str] = None

class CommentCreate(BaseModel):
    body: str

# Auth helpers
def create_token(user_id: int, email: str):
    payload = {"user_id": user_id, "email": email}
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)

def get_current_user(authorization: Optional[str] = Header(None), db=Depends(get_db)):
    if not authorization:
        raise HTTPException(status_code=401, detail="Unauthorized")
    
    try:
        payload = jwt.decode(authorization, JWT_SECRET, algorithms=[JWT_ALGORITHM])
        user_id = payload["user_id"]
        
        cursor = db.cursor(cursor_factory=RealDictCursor)
        cursor.execute("SELECT * FROM users WHERE id = %s", (user_id,))
        user = cursor.fetchone()
        cursor.close()
        
        if not user:
            raise HTTPException(status_code=401, detail="Unauthorized")
        
        return dict(user)
    except jwt.InvalidTokenError:
        raise HTTPException(status_code=401, detail="Unauthorized")

def get_current_user_optional(authorization: Optional[str] = Header(None), db=Depends(get_db)):
    if not authorization:
        return None
    
    try:
        payload = jwt.decode(authorization, JWT_SECRET, algorithms=[JWT_ALGORITHM])
        user_id = payload["user_id"]
        
        cursor = db.cursor(cursor_factory=RealDictCursor)
        cursor.execute("SELECT * FROM users WHERE id = %s", (user_id,))
        user = cursor.fetchone()
        cursor.close()
        
        return dict(user) if user else None
    except jwt.InvalidTokenError:
        return None

def slugify(title: str) -> str:
    slug = title.lower()
    slug = re.sub(r'[^a-z0-9]+', '-', slug)
    slug = slug.strip('-')
    return slug

def format_user_response(user: dict, token: str = None):
    return {
        "user": {
            "email": user["email"],
            "token": token or create_token(user["id"], user["email"]),
            "username": user["username"],
            "bio": user.get("bio"),
            "image": user.get("image")
        }
    }

def format_profile_response(user: dict, following: bool = False):
    return {
        "profile": {
            "username": user["username"],
            "bio": user.get("bio"),
            "image": user.get("image"),
            "following": following
        }
    }

def get_article_tags(article_id: int, db) -> List[str]:
    cursor = db.cursor()
    cursor.execute("SELECT tag FROM article_tags WHERE article_id = %s", (article_id,))
    tags = [row[0] for row in cursor.fetchall()]
    cursor.close()
    return tags

def is_favorited(article_id: int, user_id: Optional[int], db) -> bool:
    if not user_id:
        return False
    cursor = db.cursor()
    cursor.execute("SELECT 1 FROM favorites WHERE article_id = %s AND user_id = %s", (article_id, user_id))
    result = cursor.fetchone()
    cursor.close()
    return result is not None

def get_favorites_count(article_id: int, db) -> int:
    cursor = db.cursor()
    cursor.execute("SELECT COUNT(*) FROM favorites WHERE article_id = %s", (article_id,))
    count = cursor.fetchone()[0]
    cursor.close()
    return count

def is_following(follower_id: Optional[int], followee_id: int, db) -> bool:
    if not follower_id:
        return False
    cursor = db.cursor()
    cursor.execute("SELECT 1 FROM follows WHERE follower_id = %s AND followee_id = %s", (follower_id, followee_id))
    result = cursor.fetchone()
    cursor.close()
    return result is not None

def format_article_response(article: dict, author: dict, current_user_id: Optional[int], db):
    tags = get_article_tags(article["id"], db)
    favorited = is_favorited(article["id"], current_user_id, db)
    favorites_count = get_favorites_count(article["id"], db)
    following = is_following(current_user_id, author["id"], db)
    
    return {
        "article": {
            "slug": article["slug"],
            "title": article["title"],
            "description": article["description"],
            "body": article["body"],
            "tagList": tags,
            "createdAt": article["created_at"].isoformat().replace('+00:00', 'Z'),
            "updatedAt": article["updated_at"].isoformat().replace('+00:00', 'Z'),
            "favorited": favorited,
            "favoritesCount": favorites_count,
            "author": {
                "username": author["username"],
                "bio": author.get("bio"),
                "image": author.get("image"),
                "following": following
            }
        }
    }

def format_comment_response(comment: dict, author: dict, current_user_id: Optional[int], db):
    following = is_following(current_user_id, author["id"], db)
    
    return {
        "comment": {
            "id": comment["id"],
            "createdAt": comment["created_at"].isoformat().replace('+00:00', 'Z'),
            "updatedAt": comment["updated_at"].isoformat().replace('+00:00', 'Z'),
            "body": comment["body"],
            "author": {
                "username": author["username"],
                "bio": author.get("bio"),
                "image": author.get("image"),
                "following": following
            }
        }
    }

# Routes
@app.get("/")
async def health_check():
    return Response(status_code=200)

@app.post("/api/users")
async def register(data: dict, db=Depends(get_db)):
    from fastapi.responses import JSONResponse
    
    user_data = data.get("user")
    if not user_data:
        return JSONResponse(status_code=422, content={"errors": {"body": ["user is required"]}})
    
    username = user_data.get("username")
    email = user_data.get("email")
    password = user_data.get("password")
    
    if not username:
        return JSONResponse(status_code=422, content={"errors": {"username": ["can't be blank"]}})
    if not email:
        return JSONResponse(status_code=422, content={"errors": {"email": ["can't be blank"]}})
    if not password:
        return JSONResponse(status_code=422, content={"errors": {"password": ["can't be blank"]}})
    
    # Validate email format
    try:
        validate_email(email, check_deliverability=False)
    except EmailNotValidError:
        return JSONResponse(status_code=422, content={"errors": {"email": ["is invalid"]}})
    
    cursor = db.cursor(cursor_factory=RealDictCursor)
    
    cursor.execute("SELECT id FROM users WHERE email = %s", (email,))
    if cursor.fetchone():
        cursor.close()
        return JSONResponse(status_code=422, content={"errors": {"email": ["already taken"]}})
    
    cursor.execute("SELECT id FROM users WHERE username = %s", (username,))
    if cursor.fetchone():
        cursor.close()
        return JSONResponse(status_code=422, content={"errors": {"username": ["already taken"]}})
    
    hashed_password = bcrypt.hash(password)
    
    cursor.execute(
        "INSERT INTO users (username, email, password) VALUES (%s, %s, %s) RETURNING *",
        (username, email, hashed_password)
    )
    user = cursor.fetchone()
    db.commit()
    cursor.close()
    
    return format_user_response(dict(user))

@app.post("/api/users/login")
async def login(data: dict, db=Depends(get_db)):
    from fastapi.responses import JSONResponse
    
    user_data = data.get("user")
    if not user_data:
        return JSONResponse(status_code=422, content={"errors": {"body": ["user is required"]}})
    
    email = user_data.get("email")
    password = user_data.get("password")
    
    if not email:
        return JSONResponse(status_code=422, content={"errors": {"email": ["can't be blank"]}})
    if not password:
        return JSONResponse(status_code=422, content={"errors": {"password": ["can't be blank"]}})
    
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM users WHERE email = %s", (email,))
    user = cursor.fetchone()
    cursor.close()
    
    if not user or not bcrypt.verify(password, user["password"]):
        raise HTTPException(status_code=401, detail="Unauthorized")
    
    return format_user_response(dict(user))

@app.get("/api/user")
async def get_current_user_route(current_user: dict = Depends(get_current_user)):
    return format_user_response(current_user)

@app.put("/api/user")
async def update_user(data: dict, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    from fastapi.responses import JSONResponse
    
    user_data = data.get("user", {})
    
    updates = []
    params = []
    
    if "email" in user_data:
        # Validate email format
        try:
            validate_email(user_data["email"], check_deliverability=False)
        except EmailNotValidError:
            return JSONResponse(status_code=422, content={"errors": {"email": ["is invalid"]}})
        
        cursor = db.cursor()
        cursor.execute("SELECT id FROM users WHERE email = %s AND id != %s", (user_data["email"], current_user["id"]))
        if cursor.fetchone():
            cursor.close()
            return JSONResponse(status_code=422, content={"errors": {"email": ["already taken"]}})
        cursor.close()
        updates.append("email = %s")
        params.append(user_data["email"])
    
    if "username" in user_data:
        cursor = db.cursor()
        cursor.execute("SELECT id FROM users WHERE username = %s AND id != %s", (user_data["username"], current_user["id"]))
        if cursor.fetchone():
            cursor.close()
            return JSONResponse(status_code=422, content={"errors": {"username": ["already taken"]}})
        cursor.close()
        updates.append("username = %s")
        params.append(user_data["username"])
    
    if "password" in user_data:
        updates.append("password = %s")
        params.append(bcrypt.hash(user_data["password"]))
    
    if "bio" in user_data:
        updates.append("bio = %s")
        params.append(user_data["bio"])
    
    if "image" in user_data:
        updates.append("image = %s")
        params.append(user_data["image"])
    
    if updates:
        params.append(current_user["id"])
        cursor = db.cursor(cursor_factory=RealDictCursor)
        cursor.execute(
            f"UPDATE users SET {', '.join(updates)} WHERE id = %s RETURNING *",
            params
        )
        user = cursor.fetchone()
        db.commit()
        cursor.close()
        return format_user_response(dict(user))
    
    return format_user_response(current_user)

@app.get("/api/profiles/{username}")
async def get_profile(username: str, current_user: Optional[dict] = Depends(get_current_user_optional), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM users WHERE username = %s", (username,))
    user = cursor.fetchone()
    cursor.close()
    
    if not user:
        raise HTTPException(status_code=404, detail="Profile not found")
    
    user = dict(user)
    following = is_following(current_user["id"] if current_user else None, user["id"], db)
    
    return format_profile_response(user, following)

@app.post("/api/profiles/{username}/follow")
async def follow_user(username: str, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM users WHERE username = %s", (username,))
    user = cursor.fetchone()
    
    if not user:
        cursor.close()
        raise HTTPException(status_code=404, detail="Profile not found")
    
    user = dict(user)
    
    cursor.execute(
        "INSERT INTO follows (follower_id, followee_id) VALUES (%s, %s) ON CONFLICT DO NOTHING",
        (current_user["id"], user["id"])
    )
    db.commit()
    cursor.close()
    
    return format_profile_response(user, True)

@app.delete("/api/profiles/{username}/follow")
async def unfollow_user(username: str, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM users WHERE username = %s", (username,))
    user = cursor.fetchone()
    
    if not user:
        cursor.close()
        raise HTTPException(status_code=404, detail="Profile not found")
    
    user = dict(user)
    
    cursor.execute(
        "DELETE FROM follows WHERE follower_id = %s AND followee_id = %s",
        (current_user["id"], user["id"])
    )
    db.commit()
    cursor.close()
    
    return format_profile_response(user, False)

@app.get("/api/articles")
async def list_articles(
    tag: Optional[str] = None,
    author: Optional[str] = None,
    favorited: Optional[str] = None,
    limit: int = 20,
    offset: int = 0,
    current_user: Optional[dict] = Depends(get_current_user_optional),
    db=Depends(get_db)
):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    
    query = "SELECT DISTINCT a.* FROM articles a JOIN users u ON a.author_id = u.id"
    conditions = []
    params = []
    
    if tag:
        query += " JOIN article_tags at ON a.id = at.article_id"
        conditions.append("at.tag = %s")
        params.append(tag)
    
    if author:
        conditions.append("u.username = %s")
        params.append(author)
    
    if favorited:
        query += " JOIN favorites f ON a.id = f.article_id JOIN users fu ON f.user_id = fu.id"
        conditions.append("fu.username = %s")
        params.append(favorited)
    
    if conditions:
        query += " WHERE " + " AND ".join(conditions)
    
    query += " ORDER BY a.created_at DESC LIMIT %s OFFSET %s"
    params.extend([limit, offset])
    
    cursor.execute(query, params)
    articles = cursor.fetchall()
    
    result = []
    for article in articles:
        article = dict(article)
        cursor.execute("SELECT * FROM users WHERE id = %s", (article["author_id"],))
        author_data = dict(cursor.fetchone())
        result.append(format_article_response(article, author_data, current_user["id"] if current_user else None, db)["article"])
    
    cursor.close()
    
    return {"articles": result, "articlesCount": len(result)}

@app.get("/api/articles/feed")
async def feed_articles(
    limit: int = 20,
    offset: int = 0,
    current_user: dict = Depends(get_current_user),
    db=Depends(get_db)
):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    
    cursor.execute(
        """
        SELECT a.* FROM articles a
        JOIN follows f ON a.author_id = f.followee_id
        WHERE f.follower_id = %s
        ORDER BY a.created_at DESC
        LIMIT %s OFFSET %s
        """,
        (current_user["id"], limit, offset)
    )
    articles = cursor.fetchall()
    
    result = []
    for article in articles:
        article = dict(article)
        cursor.execute("SELECT * FROM users WHERE id = %s", (article["author_id"],))
        author = dict(cursor.fetchone())
        result.append(format_article_response(article, author, current_user["id"], db)["article"])
    
    cursor.close()
    
    return {"articles": result, "articlesCount": len(result)}

@app.get("/api/articles/{slug}")
async def get_article(slug: str, current_user: Optional[dict] = Depends(get_current_user_optional), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    cursor.execute("SELECT * FROM users WHERE id = %s", (article["author_id"],))
    author = dict(cursor.fetchone())
    cursor.close()
    
    return format_article_response(article, author, current_user["id"] if current_user else None, db)

@app.post("/api/articles")
async def create_article(data: dict, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    from fastapi.responses import JSONResponse
    
    article_data = data.get("article")
    if not article_data:
        return JSONResponse(status_code=422, content={"errors": {"body": ["article is required"]}})
    
    title = article_data.get("title")
    description = article_data.get("description")
    body = article_data.get("body")
    tag_list = article_data.get("tagList", [])
    
    if not title:
        return JSONResponse(status_code=422, content={"errors": {"title": ["can't be blank"]}})
    if not description:
        return JSONResponse(status_code=422, content={"errors": {"description": ["can't be blank"]}})
    if not body:
        return JSONResponse(status_code=422, content={"errors": {"body": ["can't be blank"]}})
    
    slug = slugify(title)
    
    cursor = db.cursor(cursor_factory=RealDictCursor)
    
    # Check for duplicate slug and make unique
    base_slug = slug
    counter = 1
    while True:
        cursor.execute("SELECT id FROM articles WHERE slug = %s", (slug,))
        if not cursor.fetchone():
            break
        slug = f"{base_slug}-{counter}"
        counter += 1
    
    cursor.execute(
        "INSERT INTO articles (slug, title, description, body, author_id) VALUES (%s, %s, %s, %s, %s) RETURNING *",
        (slug, title, description, body, current_user["id"])
    )
    article = dict(cursor.fetchone())
    
    for tag in tag_list:
        cursor.execute(
            "INSERT INTO article_tags (article_id, tag) VALUES (%s, %s)",
            (article["id"], tag)
        )
    
    db.commit()
    
    cursor.execute("SELECT * FROM users WHERE id = %s", (current_user["id"],))
    author = dict(cursor.fetchone())
    cursor.close()
    
    return format_article_response(article, author, current_user["id"], db)

@app.put("/api/articles/{slug}")
async def update_article(slug: str, data: dict, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    if article["author_id"] != current_user["id"]:
        cursor.close()
        raise HTTPException(status_code=403, detail="Forbidden")
    
    article_data = data.get("article", {})
    
    updates = ["updated_at = CURRENT_TIMESTAMP"]
    params = []
    
    if "title" in article_data:
        new_slug = slugify(article_data["title"])
        # Check for duplicate slug and make unique
        base_slug = new_slug
        counter = 1
        while True:
            cursor.execute("SELECT id FROM articles WHERE slug = %s AND id != %s", (new_slug, article["id"]))
            if not cursor.fetchone():
                break
            new_slug = f"{base_slug}-{counter}"
            counter += 1
        
        updates.append("title = %s")
        params.append(article_data["title"])
        updates.append("slug = %s")
        params.append(new_slug)
    
    if "description" in article_data:
        updates.append("description = %s")
        params.append(article_data["description"])
    
    if "body" in article_data:
        updates.append("body = %s")
        params.append(article_data["body"])
    
    params.append(article["id"])
    
    cursor.execute(
        f"UPDATE articles SET {', '.join(updates)} WHERE id = %s RETURNING *",
        params
    )
    updated_article = dict(cursor.fetchone())
    db.commit()
    
    cursor.execute("SELECT * FROM users WHERE id = %s", (current_user["id"],))
    author = dict(cursor.fetchone())
    cursor.close()
    
    return format_article_response(updated_article, author, current_user["id"], db)

@app.delete("/api/articles/{slug}")
async def delete_article(slug: str, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    if article["author_id"] != current_user["id"]:
        cursor.close()
        raise HTTPException(status_code=403, detail="Forbidden")
    
    cursor.execute("DELETE FROM articles WHERE id = %s", (article["id"],))
    db.commit()
    cursor.close()
    
    return Response(status_code=200)

@app.post("/api/articles/{slug}/comments")
async def create_comment(slug: str, data: dict, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    from fastapi.responses import JSONResponse
    
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    comment_data = data.get("comment")
    if not comment_data:
        cursor.close()
        return JSONResponse(status_code=422, content={"errors": {"body": ["comment is required"]}})
    
    body = comment_data.get("body")
    if not body:
        cursor.close()
        return JSONResponse(status_code=422, content={"errors": {"body": ["can't be blank"]}})
    
    cursor.execute(
        "INSERT INTO comments (body, article_id, author_id) VALUES (%s, %s, %s) RETURNING *",
        (body, article["id"], current_user["id"])
    )
    comment = dict(cursor.fetchone())
    db.commit()
    
    cursor.execute("SELECT * FROM users WHERE id = %s", (current_user["id"],))
    author = dict(cursor.fetchone())
    cursor.close()
    
    return format_comment_response(comment, author, current_user["id"], db)

@app.get("/api/articles/{slug}/comments")
async def get_comments(slug: str, current_user: Optional[dict] = Depends(get_current_user_optional), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    cursor.execute(
        "SELECT * FROM comments WHERE article_id = %s ORDER BY created_at ASC",
        (article["id"],)
    )
    comments = cursor.fetchall()
    
    result = []
    for comment in comments:
        comment = dict(comment)
        cursor.execute("SELECT * FROM users WHERE id = %s", (comment["author_id"],))
        author = dict(cursor.fetchone())
        result.append(format_comment_response(comment, author, current_user["id"] if current_user else None, db)["comment"])
    
    cursor.close()
    
    return {"comments": result}

@app.delete("/api/articles/{slug}/comments/{comment_id}")
async def delete_comment(slug: str, comment_id: int, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    cursor.execute("SELECT * FROM comments WHERE id = %s AND article_id = %s", (comment_id, article["id"]))
    comment = cursor.fetchone()
    
    if not comment:
        cursor.close()
        raise HTTPException(status_code=404, detail="Comment not found")
    
    comment = dict(comment)
    
    if comment["author_id"] != current_user["id"]:
        cursor.close()
        raise HTTPException(status_code=403, detail="Forbidden")
    
    cursor.execute("DELETE FROM comments WHERE id = %s", (comment_id,))
    db.commit()
    cursor.close()
    
    return Response(status_code=200)

@app.post("/api/articles/{slug}/favorite")
async def favorite_article(slug: str, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    cursor.execute(
        "INSERT INTO favorites (user_id, article_id) VALUES (%s, %s) ON CONFLICT DO NOTHING",
        (current_user["id"], article["id"])
    )
    db.commit()
    
    cursor.execute("SELECT * FROM users WHERE id = %s", (article["author_id"],))
    author = dict(cursor.fetchone())
    cursor.close()
    
    return format_article_response(article, author, current_user["id"], db)

@app.delete("/api/articles/{slug}/favorite")
async def unfavorite_article(slug: str, current_user: dict = Depends(get_current_user), db=Depends(get_db)):
    cursor = db.cursor(cursor_factory=RealDictCursor)
    cursor.execute("SELECT * FROM articles WHERE slug = %s", (slug,))
    article = cursor.fetchone()
    
    if not article:
        cursor.close()
        raise HTTPException(status_code=404, detail="Article not found")
    
    article = dict(article)
    
    cursor.execute(
        "DELETE FROM favorites WHERE user_id = %s AND article_id = %s",
        (current_user["id"], article["id"])
    )
    db.commit()
    
    cursor.execute("SELECT * FROM users WHERE id = %s", (article["author_id"],))
    author = dict(cursor.fetchone())
    cursor.close()
    
    return format_article_response(article, author, current_user["id"], db)

@app.get("/api/tags")
async def get_tags(db=Depends(get_db)):
    cursor = db.cursor()
    cursor.execute("SELECT DISTINCT tag FROM article_tags ORDER BY tag")
    tags = [row[0] for row in cursor.fetchall()]
    cursor.close()
    
    return {"tags": tags}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=3000)
