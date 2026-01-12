package main

import (
	"database/sql"
	"fmt"
	"log"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
	_ "github.com/lib/pq"
	"golang.org/x/crypto/bcrypt"
)

var db *sql.DB
var jwtSecret = []byte("secret-key-for-jwt")

type User struct {
	ID        int       `json:"id"`
	Username  string    `json:"username"`
	Email     string    `json:"email"`
	Password  string    `json:"-"`
	Bio       *string   `json:"bio"`
	Image     *string   `json:"image"`
	CreatedAt time.Time `json:"created_at"`
}

type Article struct {
	ID          int       `json:"id"`
	Slug        string    `json:"slug"`
	Title       string    `json:"title"`
	Description string    `json:"description"`
	Body        string    `json:"body"`
	AuthorID    int       `json:"author_id"`
	CreatedAt   time.Time `json:"createdAt"`
	UpdatedAt   time.Time `json:"updatedAt"`
}

type Comment struct {
	ID        int       `json:"id"`
	Body      string    `json:"body"`
	ArticleID int       `json:"article_id"`
	AuthorID  int       `json:"author_id"`
	CreatedAt time.Time `json:"createdAt"`
	UpdatedAt time.Time `json:"updatedAt"`
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "3000"
	}

	postgresURI := os.Getenv("POSTGRES_URI")
	if postgresURI == "" {
		postgresURI = "postgres://postgres:password@localhost:5432/postgres?sslmode=disable"
	}

	if secret := os.Getenv("JWT_SECRET"); secret != "" {
		jwtSecret = []byte(secret)
	}

	var err error
	db, err = sql.Open("postgres", postgresURI)
	if err != nil {
		log.Fatal("Failed to connect to database:", err)
	}
	defer db.Close()

	if err := db.Ping(); err != nil {
		log.Fatal("Failed to ping database:", err)
	}

	if err := initDB(); err != nil {
		log.Fatal("Failed to initialize database:", err)
	}

	gin.SetMode(gin.ReleaseMode)
	r := gin.Default()

	r.GET("/", healthCheck)
	r.POST("/api/users", registerUser)
	r.POST("/api/users/login", loginUser)
	r.GET("/api/user", authRequired(), getCurrentUser)
	r.PUT("/api/user", authRequired(), updateUser)
	r.GET("/api/profiles/:username", optionalAuth(), getProfile)
	r.POST("/api/profiles/:username/follow", authRequired(), followUser)
	r.DELETE("/api/profiles/:username/follow", authRequired(), unfollowUser)
	r.GET("/api/articles", optionalAuth(), listArticles)
	r.GET("/api/articles/feed", authRequired(), feedArticles)
	r.POST("/api/articles", authRequired(), createArticle)
	r.GET("/api/articles/:slug", optionalAuth(), getArticle)
	r.PUT("/api/articles/:slug", authRequired(), updateArticle)
	r.DELETE("/api/articles/:slug", authRequired(), deleteArticle)
	r.POST("/api/articles/:slug/comments", authRequired(), addComment)
	r.GET("/api/articles/:slug/comments", optionalAuth(), getComments)
	r.DELETE("/api/articles/:slug/comments/:id", authRequired(), deleteComment)
	r.POST("/api/articles/:slug/favorite", authRequired(), favoriteArticle)
	r.DELETE("/api/articles/:slug/favorite", authRequired(), unfavoriteArticle)
	r.GET("/api/tags", getTags)

	fmt.Printf("Server running on port %s\n", port)
	if err := r.Run(":" + port); err != nil {
		log.Fatal("Failed to start server:", err)
	}
}

func initDB() error {
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS users (
			id SERIAL PRIMARY KEY,
			username VARCHAR(255) UNIQUE NOT NULL,
			email VARCHAR(255) UNIQUE NOT NULL,
			password VARCHAR(255) NOT NULL,
			bio TEXT,
			image TEXT,
			created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
		)
	`)
	if err != nil {
		return err
	}

	_, err = db.Exec(`
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
	`)
	if err != nil {
		return err
	}

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS article_tags (
			article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
			tag VARCHAR(255) NOT NULL,
			PRIMARY KEY (article_id, tag)
		)
	`)
	if err != nil {
		return err
	}

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS comments (
			id SERIAL PRIMARY KEY,
			body TEXT NOT NULL,
			article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
			author_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
			created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
			updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
		)
	`)
	if err != nil {
		return err
	}

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS favorites (
			user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
			article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
			PRIMARY KEY (user_id, article_id)
		)
	`)
	if err != nil {
		return err
	}

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS follows (
			follower_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
			following_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
			PRIMARY KEY (follower_id, following_id)
		)
	`)
	return err
}

func healthCheck(c *gin.Context) {
	c.Status(200)
}

func authRequired() gin.HandlerFunc {
	return func(c *gin.Context) {
		token := c.GetHeader("Authorization")
		if token == "" {
			c.JSON(401, gin.H{"errors": gin.H{"body": []string{"Unauthorized"}}})
			c.Abort()
			return
		}

		claims := &jwt.RegisteredClaims{}
		tkn, err := jwt.ParseWithClaims(token, claims, func(token *jwt.Token) (interface{}, error) {
			return jwtSecret, nil
		})

		if err != nil || !tkn.Valid {
			c.JSON(401, gin.H{"errors": gin.H{"body": []string{"Unauthorized"}}})
			c.Abort()
			return
		}

		userID, err := strconv.Atoi(claims.Subject)
		if err != nil {
			c.JSON(401, gin.H{"errors": gin.H{"body": []string{"Unauthorized"}}})
			c.Abort()
			return
		}

		c.Set("userID", userID)
		c.Next()
	}
}

func optionalAuth() gin.HandlerFunc {
	return func(c *gin.Context) {
		token := c.GetHeader("Authorization")
		if token != "" {
			claims := &jwt.RegisteredClaims{}
			tkn, err := jwt.ParseWithClaims(token, claims, func(token *jwt.Token) (interface{}, error) {
				return jwtSecret, nil
			})

			if err == nil && tkn.Valid {
				if userID, err := strconv.Atoi(claims.Subject); err == nil {
					c.Set("userID", userID)
				}
			}
		}
		c.Next()
	}
}

func slugify(title string) string {
	slug := strings.ToLower(title)
	reg := regexp.MustCompile("[^a-z0-9\\s-]")
	slug = reg.ReplaceAllString(slug, "")
	slug = strings.TrimSpace(slug)
	slug = regexp.MustCompile("\\s+").ReplaceAllString(slug, "-")
	slug = regexp.MustCompile("-+").ReplaceAllString(slug, "-")
	return slug
}

func generateToken(userID int) (string, error) {
	claims := jwt.RegisteredClaims{
		Subject:   strconv.Itoa(userID),
		ExpiresAt: jwt.NewNumericDate(time.Now().Add(24 * time.Hour * 365)),
	}
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString(jwtSecret)
}

func isValidEmail(email string) bool {
	emailRegex := regexp.MustCompile(`^[^\s@]+@[^\s@]+\.[^\s@]+$`)
	return emailRegex.MatchString(email)
}

func formatUser(user User, token string) map[string]interface{} {
	bio := ""
	if user.Bio != nil {
		bio = *user.Bio
	}
	return map[string]interface{}{
		"user": map[string]interface{}{
			"email":    user.Email,
			"token":    token,
			"username": user.Username,
			"bio":      bio,
			"image":    user.Image,
		},
	}
}

func formatProfile(user User, currentUserID int) map[string]interface{} {
	following := false
	if currentUserID > 0 {
		var count int
		db.QueryRow("SELECT COUNT(*) FROM follows WHERE follower_id = $1 AND following_id = $2",
			currentUserID, user.ID).Scan(&count)
		following = count > 0
	}

	bio := ""
	if user.Bio != nil {
		bio = *user.Bio
	}

	return map[string]interface{}{
		"profile": map[string]interface{}{
			"username":  user.Username,
			"bio":       bio,
			"image":     user.Image,
			"following": following,
		},
	}
}

func formatArticle(article Article, currentUserID int) (map[string]interface{}, error) {
	var author User
	err := db.QueryRow("SELECT id, username, email, bio, image FROM users WHERE id = $1",
		article.AuthorID).Scan(&author.ID, &author.Username, &author.Email, &author.Bio, &author.Image)
	if err != nil {
		return nil, err
	}

	authorProfile := formatProfile(author, currentUserID)

	rows, err := db.Query("SELECT tag FROM article_tags WHERE article_id = $1", article.ID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var tagList []string
	for rows.Next() {
		var tag string
		if err := rows.Scan(&tag); err != nil {
			return nil, err
		}
		tagList = append(tagList, tag)
	}

	if tagList == nil {
		tagList = []string{}
	}

	favorited := false
	if currentUserID > 0 {
		var count int
		db.QueryRow("SELECT COUNT(*) FROM favorites WHERE user_id = $1 AND article_id = $2",
			currentUserID, article.ID).Scan(&count)
		favorited = count > 0
	}

	var favoritesCount int
	db.QueryRow("SELECT COUNT(*) FROM favorites WHERE article_id = $1", article.ID).Scan(&favoritesCount)

	return map[string]interface{}{
		"slug":           article.Slug,
		"title":          article.Title,
		"description":    article.Description,
		"body":           article.Body,
		"tagList":        tagList,
		"createdAt":      article.CreatedAt.Format(time.RFC3339),
		"updatedAt":      article.UpdatedAt.Format(time.RFC3339),
		"favorited":      favorited,
		"favoritesCount": favoritesCount,
		"author":         authorProfile["profile"],
	}, nil
}

func formatComment(comment Comment, currentUserID int) (map[string]interface{}, error) {
	var author User
	err := db.QueryRow("SELECT id, username, email, bio, image FROM users WHERE id = $1",
		comment.AuthorID).Scan(&author.ID, &author.Username, &author.Email, &author.Bio, &author.Image)
	if err != nil {
		return nil, err
	}

	authorProfile := formatProfile(author, currentUserID)

	return map[string]interface{}{
		"id":        comment.ID,
		"createdAt": comment.CreatedAt.Format(time.RFC3339),
		"updatedAt": comment.UpdatedAt.Format(time.RFC3339),
		"body":      comment.Body,
		"author":    authorProfile["profile"],
	}, nil
}

func registerUser(c *gin.Context) {
	var req struct {
		User struct {
			Username string `json:"username"`
			Email    string `json:"email"`
			Password string `json:"password"`
		} `json:"user"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	if req.User.Username == "" || req.User.Email == "" || req.User.Password == "" {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{"Missing required fields"}}})
		return
	}

	hashedPassword, err := bcrypt.GenerateFromPassword([]byte(req.User.Password), bcrypt.DefaultCost)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	var user User
	err = db.QueryRow(`
		INSERT INTO users (username, email, password)
		VALUES ($1, $2, $3)
		RETURNING id, username, email, bio, image, created_at
	`, req.User.Username, req.User.Email, string(hashedPassword)).Scan(
		&user.ID, &user.Username, &user.Email, &user.Bio, &user.Image, &user.CreatedAt)

	if err != nil {
		if strings.Contains(err.Error(), "duplicate key") {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{"Username or email already exists"}}})
		} else {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		}
		return
	}

	token, err := generateToken(user.ID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, formatUser(user, token))
}

func loginUser(c *gin.Context) {
	var req struct {
		User struct {
			Email    string `json:"email"`
			Password string `json:"password"`
		} `json:"user"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	if req.User.Email == "" || req.User.Password == "" {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{"Missing required fields"}}})
		return
	}

	var user User
	var hashedPassword string
	err := db.QueryRow("SELECT id, username, email, password, bio, image FROM users WHERE email = $1",
		req.User.Email).Scan(&user.ID, &user.Username, &user.Email, &hashedPassword, &user.Bio, &user.Image)

	if err != nil {
		c.JSON(401, gin.H{"errors": gin.H{"body": []string{"Invalid email or password"}}})
		return
	}

	if err := bcrypt.CompareHashAndPassword([]byte(hashedPassword), []byte(req.User.Password)); err != nil {
		c.JSON(401, gin.H{"errors": gin.H{"body": []string{"Invalid email or password"}}})
		return
	}

	token, err := generateToken(user.ID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, formatUser(user, token))
}

func getCurrentUser(c *gin.Context) {
	userID := c.GetInt("userID")

	var user User
	err := db.QueryRow("SELECT id, username, email, bio, image FROM users WHERE id = $1",
		userID).Scan(&user.ID, &user.Username, &user.Email, &user.Bio, &user.Image)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	token := c.GetHeader("Authorization")
	c.JSON(200, formatUser(user, token))
}

func updateUser(c *gin.Context) {
	userID := c.GetInt("userID")

	var req struct {
		User struct {
			Email    *string `json:"email"`
			Username *string `json:"username"`
			Password *string `json:"password"`
			Bio      *string `json:"bio"`
			Image    *string `json:"image"`
		} `json:"user"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	updates := []string{}
	args := []interface{}{}
	argCount := 1

	if req.User.Email != nil {
		if !isValidEmail(*req.User.Email) {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{"Invalid email format"}}})
			return
		}
		updates = append(updates, fmt.Sprintf("email = $%d", argCount))
		args = append(args, *req.User.Email)
		argCount++
	}
	if req.User.Username != nil {
		updates = append(updates, fmt.Sprintf("username = $%d", argCount))
		args = append(args, *req.User.Username)
		argCount++
	}
	if req.User.Password != nil {
		hashedPassword, err := bcrypt.GenerateFromPassword([]byte(*req.User.Password), bcrypt.DefaultCost)
		if err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}
		updates = append(updates, fmt.Sprintf("password = $%d", argCount))
		args = append(args, string(hashedPassword))
		argCount++
	}
	if req.User.Bio != nil {
		updates = append(updates, fmt.Sprintf("bio = $%d", argCount))
		args = append(args, *req.User.Bio)
		argCount++
	}
	if req.User.Image != nil {
		updates = append(updates, fmt.Sprintf("image = $%d", argCount))
		args = append(args, *req.User.Image)
		argCount++
	}

	args = append(args, userID)
	query := fmt.Sprintf("UPDATE users SET %s WHERE id = $%d RETURNING id, username, email, bio, image",
		strings.Join(updates, ", "), argCount)

	var user User
	err := db.QueryRow(query, args...).Scan(&user.ID, &user.Username, &user.Email, &user.Bio, &user.Image)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	token := c.GetHeader("Authorization")
	c.JSON(200, formatUser(user, token))
}

func getProfile(c *gin.Context) {
	username := c.Param("username")
	currentUserID := c.GetInt("userID")

	var user User
	err := db.QueryRow("SELECT id, username, email, bio, image FROM users WHERE username = $1",
		username).Scan(&user.ID, &user.Username, &user.Email, &user.Bio, &user.Image)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"User not found"}}})
		return
	}

	c.JSON(200, formatProfile(user, currentUserID))
}

func followUser(c *gin.Context) {
	username := c.Param("username")
	currentUserID := c.GetInt("userID")

	var user User
	err := db.QueryRow("SELECT id, username, email, bio, image FROM users WHERE username = $1",
		username).Scan(&user.ID, &user.Username, &user.Email, &user.Bio, &user.Image)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"User not found"}}})
		return
	}

	_, err = db.Exec("INSERT INTO follows (follower_id, following_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
		currentUserID, user.ID)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, formatProfile(user, currentUserID))
}

func unfollowUser(c *gin.Context) {
	username := c.Param("username")
	currentUserID := c.GetInt("userID")

	var user User
	err := db.QueryRow("SELECT id, username, email, bio, image FROM users WHERE username = $1",
		username).Scan(&user.ID, &user.Username, &user.Email, &user.Bio, &user.Image)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"User not found"}}})
		return
	}

	_, err = db.Exec("DELETE FROM follows WHERE follower_id = $1 AND following_id = $2",
		currentUserID, user.ID)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, formatProfile(user, currentUserID))
}

func listArticles(c *gin.Context) {
	currentUserID := c.GetInt("userID")
	tag := c.Query("tag")
	author := c.Query("author")
	favorited := c.Query("favorited")
	limit := c.DefaultQuery("limit", "20")
	offset := c.DefaultQuery("offset", "0")

	query := `
		SELECT DISTINCT a.id, a.slug, a.title, a.description, a.body, a.author_id, a.created_at, a.updated_at
		FROM articles a
		LEFT JOIN article_tags at ON a.id = at.article_id
		LEFT JOIN users u ON a.author_id = u.id
		LEFT JOIN favorites f ON a.id = f.article_id
		LEFT JOIN users fu ON f.user_id = fu.id
		WHERE 1=1
	`
	args := []interface{}{}
	argCount := 1

	if tag != "" {
		query += fmt.Sprintf(" AND at.tag = $%d", argCount)
		args = append(args, tag)
		argCount++
	}

	if author != "" {
		query += fmt.Sprintf(" AND u.username = $%d", argCount)
		args = append(args, author)
		argCount++
	}

	if favorited != "" {
		query += fmt.Sprintf(" AND fu.username = $%d", argCount)
		args = append(args, favorited)
		argCount++
	}

	query += fmt.Sprintf(" ORDER BY a.created_at DESC LIMIT $%d OFFSET $%d", argCount, argCount+1)
	args = append(args, limit, offset)

	rows, err := db.Query(query, args...)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}
	defer rows.Close()

	articles := []map[string]interface{}{}
	for rows.Next() {
		var article Article
		if err := rows.Scan(&article.ID, &article.Slug, &article.Title, &article.Description,
			&article.Body, &article.AuthorID, &article.CreatedAt, &article.UpdatedAt); err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}

		formattedArticle, err := formatArticle(article, currentUserID)
		if err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}
		articles = append(articles, formattedArticle)
	}

	if articles == nil {
		articles = []map[string]interface{}{}
	}

	c.JSON(200, gin.H{
		"articles":      articles,
		"articlesCount": len(articles),
	})
}

func feedArticles(c *gin.Context) {
	currentUserID := c.GetInt("userID")
	limit := c.DefaultQuery("limit", "20")
	offset := c.DefaultQuery("offset", "0")

	query := `
		SELECT a.id, a.slug, a.title, a.description, a.body, a.author_id, a.created_at, a.updated_at
		FROM articles a
		JOIN follows f ON a.author_id = f.following_id
		WHERE f.follower_id = $1
		ORDER BY a.created_at DESC
		LIMIT $2 OFFSET $3
	`

	rows, err := db.Query(query, currentUserID, limit, offset)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}
	defer rows.Close()

	articles := []map[string]interface{}{}
	for rows.Next() {
		var article Article
		if err := rows.Scan(&article.ID, &article.Slug, &article.Title, &article.Description,
			&article.Body, &article.AuthorID, &article.CreatedAt, &article.UpdatedAt); err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}

		formattedArticle, err := formatArticle(article, currentUserID)
		if err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}
		articles = append(articles, formattedArticle)
	}

	if articles == nil {
		articles = []map[string]interface{}{}
	}

	c.JSON(200, gin.H{
		"articles":      articles,
		"articlesCount": len(articles),
	})
}

func getArticle(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var article Article
	err := db.QueryRow("SELECT id, slug, title, description, body, author_id, created_at, updated_at FROM articles WHERE slug = $1",
		slug).Scan(&article.ID, &article.Slug, &article.Title, &article.Description, &article.Body,
		&article.AuthorID, &article.CreatedAt, &article.UpdatedAt)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	formattedArticle, err := formatArticle(article, currentUserID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{"article": formattedArticle})
}

func createArticle(c *gin.Context) {
	currentUserID := c.GetInt("userID")

	var req struct {
		Article struct {
			Title       string   `json:"title"`
			Description string   `json:"description"`
			Body        string   `json:"body"`
			TagList     []string `json:"tagList"`
		} `json:"article"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	if req.Article.Title == "" || req.Article.Description == "" || req.Article.Body == "" {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{"Missing required fields"}}})
		return
	}

	slug := slugify(req.Article.Title)

	var existingCount int
	db.QueryRow("SELECT COUNT(*) FROM articles WHERE slug = $1", slug).Scan(&existingCount)
	if existingCount > 0 {
		slug = fmt.Sprintf("%s-%d", slug, time.Now().UnixMilli())
	}

	var article Article
	err := db.QueryRow(`
		INSERT INTO articles (slug, title, description, body, author_id)
		VALUES ($1, $2, $3, $4, $5)
		RETURNING id, slug, title, description, body, author_id, created_at, updated_at
	`, slug, req.Article.Title, req.Article.Description, req.Article.Body, currentUserID).Scan(
		&article.ID, &article.Slug, &article.Title, &article.Description, &article.Body,
		&article.AuthorID, &article.CreatedAt, &article.UpdatedAt)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	for _, tag := range req.Article.TagList {
		_, err := db.Exec("INSERT INTO article_tags (article_id, tag) VALUES ($1, $2)", article.ID, tag)
		if err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}
	}

	formattedArticle, err := formatArticle(article, currentUserID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{"article": formattedArticle})
}

func updateArticle(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var req struct {
		Article struct {
			Title       *string `json:"title"`
			Description *string `json:"description"`
			Body        *string `json:"body"`
		} `json:"article"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	var article Article
	err := db.QueryRow("SELECT id, slug, title, description, body, author_id, created_at, updated_at FROM articles WHERE slug = $1",
		slug).Scan(&article.ID, &article.Slug, &article.Title, &article.Description, &article.Body,
		&article.AuthorID, &article.CreatedAt, &article.UpdatedAt)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	if article.AuthorID != currentUserID {
		c.JSON(403, gin.H{"errors": gin.H{"body": []string{"Forbidden"}}})
		return
	}

	updates := []string{"updated_at = CURRENT_TIMESTAMP"}
	args := []interface{}{}
	argCount := 1

	if req.Article.Title != nil {
		updates = append(updates, fmt.Sprintf("title = $%d", argCount))
		args = append(args, *req.Article.Title)
		argCount++

		newSlug := slugify(*req.Article.Title)
		var existingCount int
		db.QueryRow("SELECT COUNT(*) FROM articles WHERE slug = $1 AND id != $2", newSlug, article.ID).Scan(&existingCount)
		if existingCount > 0 {
			newSlug = fmt.Sprintf("%s-%d", newSlug, time.Now().UnixMilli())
		}

		updates = append(updates, fmt.Sprintf("slug = $%d", argCount))
		args = append(args, newSlug)
		argCount++
	}
	if req.Article.Description != nil {
		updates = append(updates, fmt.Sprintf("description = $%d", argCount))
		args = append(args, *req.Article.Description)
		argCount++
	}
	if req.Article.Body != nil {
		updates = append(updates, fmt.Sprintf("body = $%d", argCount))
		args = append(args, *req.Article.Body)
		argCount++
	}

	args = append(args, slug)
	query := fmt.Sprintf("UPDATE articles SET %s WHERE slug = $%d RETURNING id, slug, title, description, body, author_id, created_at, updated_at",
		strings.Join(updates, ", "), argCount)

	err = db.QueryRow(query, args...).Scan(&article.ID, &article.Slug, &article.Title, &article.Description,
		&article.Body, &article.AuthorID, &article.CreatedAt, &article.UpdatedAt)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	formattedArticle, err := formatArticle(article, currentUserID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{"article": formattedArticle})
}

func deleteArticle(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var article Article
	err := db.QueryRow("SELECT id, author_id FROM articles WHERE slug = $1",
		slug).Scan(&article.ID, &article.AuthorID)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	if article.AuthorID != currentUserID {
		c.JSON(403, gin.H{"errors": gin.H{"body": []string{"Forbidden"}}})
		return
	}

	_, err = db.Exec("DELETE FROM articles WHERE slug = $1", slug)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{})
}

func addComment(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var req struct {
		Comment struct {
			Body string `json:"body"`
		} `json:"comment"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	if req.Comment.Body == "" {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{"Missing required fields"}}})
		return
	}

	var articleID int
	err := db.QueryRow("SELECT id FROM articles WHERE slug = $1", slug).Scan(&articleID)
	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	var comment Comment
	err = db.QueryRow(`
		INSERT INTO comments (body, article_id, author_id)
		VALUES ($1, $2, $3)
		RETURNING id, body, article_id, author_id, created_at, updated_at
	`, req.Comment.Body, articleID, currentUserID).Scan(
		&comment.ID, &comment.Body, &comment.ArticleID, &comment.AuthorID,
		&comment.CreatedAt, &comment.UpdatedAt)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	formattedComment, err := formatComment(comment, currentUserID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{"comment": formattedComment})
}

func getComments(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var articleID int
	err := db.QueryRow("SELECT id FROM articles WHERE slug = $1", slug).Scan(&articleID)
	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	rows, err := db.Query(`
		SELECT id, body, article_id, author_id, created_at, updated_at
		FROM comments
		WHERE article_id = $1
		ORDER BY created_at DESC
	`, articleID)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}
	defer rows.Close()

	comments := []map[string]interface{}{}
	for rows.Next() {
		var comment Comment
		if err := rows.Scan(&comment.ID, &comment.Body, &comment.ArticleID, &comment.AuthorID,
			&comment.CreatedAt, &comment.UpdatedAt); err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}

		formattedComment, err := formatComment(comment, currentUserID)
		if err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}
		comments = append(comments, formattedComment)
	}

	if comments == nil {
		comments = []map[string]interface{}{}
	}

	c.JSON(200, gin.H{"comments": comments})
}

func deleteComment(c *gin.Context) {
	slug := c.Param("slug")
	commentID := c.Param("id")
	currentUserID := c.GetInt("userID")

	var articleID int
	err := db.QueryRow("SELECT id FROM articles WHERE slug = $1", slug).Scan(&articleID)
	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	var comment Comment
	err = db.QueryRow("SELECT id, author_id FROM comments WHERE id = $1", commentID).Scan(
		&comment.ID, &comment.AuthorID)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Comment not found"}}})
		return
	}

	if comment.AuthorID != currentUserID {
		c.JSON(403, gin.H{"errors": gin.H{"body": []string{"Forbidden"}}})
		return
	}

	_, err = db.Exec("DELETE FROM comments WHERE id = $1", commentID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{})
}

func favoriteArticle(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var article Article
	err := db.QueryRow("SELECT id, slug, title, description, body, author_id, created_at, updated_at FROM articles WHERE slug = $1",
		slug).Scan(&article.ID, &article.Slug, &article.Title, &article.Description, &article.Body,
		&article.AuthorID, &article.CreatedAt, &article.UpdatedAt)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	_, err = db.Exec("INSERT INTO favorites (user_id, article_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
		currentUserID, article.ID)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	formattedArticle, err := formatArticle(article, currentUserID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{"article": formattedArticle})
}

func unfavoriteArticle(c *gin.Context) {
	slug := c.Param("slug")
	currentUserID := c.GetInt("userID")

	var article Article
	err := db.QueryRow("SELECT id, slug, title, description, body, author_id, created_at, updated_at FROM articles WHERE slug = $1",
		slug).Scan(&article.ID, &article.Slug, &article.Title, &article.Description, &article.Body,
		&article.AuthorID, &article.CreatedAt, &article.UpdatedAt)

	if err != nil {
		c.JSON(404, gin.H{"errors": gin.H{"body": []string{"Article not found"}}})
		return
	}

	_, err = db.Exec("DELETE FROM favorites WHERE user_id = $1 AND article_id = $2",
		currentUserID, article.ID)

	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	formattedArticle, err := formatArticle(article, currentUserID)
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}

	c.JSON(200, gin.H{"article": formattedArticle})
}

func getTags(c *gin.Context) {
	rows, err := db.Query("SELECT DISTINCT tag FROM article_tags ORDER BY tag")
	if err != nil {
		c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
		return
	}
	defer rows.Close()

	tags := []string{}
	for rows.Next() {
		var tag string
		if err := rows.Scan(&tag); err != nil {
			c.JSON(422, gin.H{"errors": gin.H{"body": []string{err.Error()}}})
			return
		}
		tags = append(tags, tag)
	}

	if tags == nil {
		tags = []string{}
	}

	c.JSON(200, gin.H{"tags": tags})
}
