import express from 'express';
import pg from 'pg';
import jwt from 'jsonwebtoken';
import bcrypt from 'bcrypt';

const app = express();
const PORT = process.env.PORT || 3000;
const POSTGRES_URI = process.env.POSTGRES_URI || 'postgres://postgres:password@localhost:5432/postgres';
const JWT_SECRET = process.env.JWT_SECRET || 'secret-key-for-jwt';

app.use(express.json());

const pool = new pg.Pool({ connectionString: POSTGRES_URI });

// Initialize database
async function initDB() {
  const client = await pool.connect();
  try {
    await client.query(`
      CREATE TABLE IF NOT EXISTS users (
        id SERIAL PRIMARY KEY,
        username VARCHAR(255) UNIQUE NOT NULL,
        email VARCHAR(255) UNIQUE NOT NULL,
        password VARCHAR(255) NOT NULL,
        bio TEXT,
        image TEXT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    await client.query(`
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
    `);

    await client.query(`
      CREATE TABLE IF NOT EXISTS article_tags (
        article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
        tag VARCHAR(255) NOT NULL,
        PRIMARY KEY (article_id, tag)
      )
    `);

    await client.query(`
      CREATE TABLE IF NOT EXISTS comments (
        id SERIAL PRIMARY KEY,
        body TEXT NOT NULL,
        article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
        author_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    await client.query(`
      CREATE TABLE IF NOT EXISTS favorites (
        user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
        article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
        PRIMARY KEY (user_id, article_id)
      )
    `);

    await client.query(`
      CREATE TABLE IF NOT EXISTS follows (
        follower_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
        following_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
        PRIMARY KEY (follower_id, following_id)
      )
    `);
  } finally {
    client.release();
  }
}

// Auth middleware
function authenticateToken(req, res, next) {
  const token = req.headers.authorization;
  
  if (!token) {
    return res.status(401).json({ errors: { body: ['Unauthorized'] } });
  }

  try {
    const decoded = jwt.verify(token, JWT_SECRET);
    req.userId = decoded.userId;
    next();
  } catch (err) {
    return res.status(401).json({ errors: { body: ['Unauthorized'] } });
  }
}

// Optional auth middleware
function optionalAuth(req, res, next) {
  const token = req.headers.authorization;
  
  if (token) {
    try {
      const decoded = jwt.verify(token, JWT_SECRET);
      req.userId = decoded.userId;
    } catch (err) {
      // Ignore invalid tokens
    }
  }
  next();
}

// Helper functions
function slugify(title) {
  return title.toLowerCase()
    .replace(/[^\w\s-]/g, '')
    .replace(/\s+/g, '-')
    .replace(/-+/g, '-')
    .trim();
}

function isValidEmail(email) {
  const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
  return emailRegex.test(email);
}

async function getUserById(userId) {
  const result = await pool.query('SELECT * FROM users WHERE id = $1', [userId]);
  return result.rows[0];
}

async function getUserByUsername(username) {
  const result = await pool.query('SELECT * FROM users WHERE username = $1', [username]);
  return result.rows[0];
}

function formatUser(user, token) {
  return {
    user: {
      email: user.email,
      token: token,
      username: user.username,
      bio: user.bio || '',
      image: user.image || null
    }
  };
}

async function formatProfile(user, currentUserId) {
  let following = false;
  if (currentUserId) {
    const followResult = await pool.query(
      'SELECT 1 FROM follows WHERE follower_id = $1 AND following_id = $2',
      [currentUserId, user.id]
    );
    following = followResult.rows.length > 0;
  }
  
  return {
    profile: {
      username: user.username,
      bio: user.bio || '',
      image: user.image || null,
      following
    }
  };
}

async function formatArticle(article, currentUserId) {
  const author = await getUserById(article.author_id);
  const authorProfile = await formatProfile(author, currentUserId);
  
  const tagsResult = await pool.query(
    'SELECT tag FROM article_tags WHERE article_id = $1',
    [article.id]
  );
  const tagList = tagsResult.rows.map(row => row.tag);
  
  let favorited = false;
  if (currentUserId) {
    const favResult = await pool.query(
      'SELECT 1 FROM favorites WHERE user_id = $1 AND article_id = $2',
      [currentUserId, article.id]
    );
    favorited = favResult.rows.length > 0;
  }
  
  const favCountResult = await pool.query(
    'SELECT COUNT(*) FROM favorites WHERE article_id = $1',
    [article.id]
  );
  const favoritesCount = parseInt(favCountResult.rows[0].count);
  
  return {
    slug: article.slug,
    title: article.title,
    description: article.description,
    body: article.body,
    tagList,
    createdAt: article.created_at.toISOString(),
    updatedAt: article.updated_at.toISOString(),
    favorited,
    favoritesCount,
    author: authorProfile.profile
  };
}

async function formatComment(comment, currentUserId) {
  const author = await getUserById(comment.author_id);
  const authorProfile = await formatProfile(author, currentUserId);
  
  return {
    id: comment.id,
    createdAt: comment.created_at.toISOString(),
    updatedAt: comment.updated_at.toISOString(),
    body: comment.body,
    author: authorProfile.profile
  };
}

// Health check
app.get('/', (req, res) => {
  res.status(200).send();
});

// User registration
app.post('/api/users', async (req, res) => {
  try {
    const { user } = req.body;
    const { username, email, password } = user;
    
    if (!username || !email || !password) {
      return res.status(422).json({ errors: { body: ['Missing required fields'] } });
    }
    
    const hashedPassword = await bcrypt.hash(password, 10);
    
    const result = await pool.query(
      'INSERT INTO users (username, email, password) VALUES ($1, $2, $3) RETURNING *',
      [username, email, hashedPassword]
    );
    
    const newUser = result.rows[0];
    const token = jwt.sign({ userId: newUser.id }, JWT_SECRET);
    
    res.status(200).json(formatUser(newUser, token));
  } catch (err) {
    if (err.code === '23505') {
      return res.status(422).json({ errors: { body: ['Username or email already exists'] } });
    }
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// User login
app.post('/api/users/login', async (req, res) => {
  try {
    const { user } = req.body;
    const { email, password } = user;
    
    if (!email || !password) {
      return res.status(422).json({ errors: { body: ['Missing required fields'] } });
    }
    
    const result = await pool.query('SELECT * FROM users WHERE email = $1', [email]);
    
    if (result.rows.length === 0) {
      return res.status(401).json({ errors: { body: ['Invalid email or password'] } });
    }
    
    const dbUser = result.rows[0];
    const passwordMatch = await bcrypt.compare(password, dbUser.password);
    
    if (!passwordMatch) {
      return res.status(401).json({ errors: { body: ['Invalid email or password'] } });
    }
    
    const token = jwt.sign({ userId: dbUser.id }, JWT_SECRET);
    
    res.json(formatUser(dbUser, token));
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Get current user
app.get('/api/user', authenticateToken, async (req, res) => {
  try {
    const user = await getUserById(req.userId);
    const token = req.headers.authorization;
    res.json(formatUser(user, token));
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Update user
app.put('/api/user', authenticateToken, async (req, res) => {
  try {
    const { user } = req.body;
    const updates = [];
    const values = [];
    let paramCount = 1;
    
    if (user.email !== undefined) {
      if (!isValidEmail(user.email)) {
        return res.status(422).json({ errors: { body: ['Invalid email format'] } });
      }
      updates.push(`email = $${paramCount++}`);
      values.push(user.email);
    }
    if (user.username !== undefined) {
      updates.push(`username = $${paramCount++}`);
      values.push(user.username);
    }
    if (user.password !== undefined) {
      const hashedPassword = await bcrypt.hash(user.password, 10);
      updates.push(`password = $${paramCount++}`);
      values.push(hashedPassword);
    }
    if (user.bio !== undefined) {
      updates.push(`bio = $${paramCount++}`);
      values.push(user.bio);
    }
    if (user.image !== undefined) {
      updates.push(`image = $${paramCount++}`);
      values.push(user.image);
    }
    
    values.push(req.userId);
    
    const result = await pool.query(
      `UPDATE users SET ${updates.join(', ')} WHERE id = $${paramCount} RETURNING *`,
      values
    );
    
    const updatedUser = result.rows[0];
    const token = req.headers.authorization;
    
    res.json(formatUser(updatedUser, token));
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Get profile
app.get('/api/profiles/:username', optionalAuth, async (req, res) => {
  try {
    const user = await getUserByUsername(req.params.username);
    
    if (!user) {
      return res.status(404).json({ errors: { body: ['User not found'] } });
    }
    
    const profile = await formatProfile(user, req.userId);
    res.json(profile);
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Follow user
app.post('/api/profiles/:username/follow', authenticateToken, async (req, res) => {
  try {
    const user = await getUserByUsername(req.params.username);
    
    if (!user) {
      return res.status(404).json({ errors: { body: ['User not found'] } });
    }
    
    await pool.query(
      'INSERT INTO follows (follower_id, following_id) VALUES ($1, $2) ON CONFLICT DO NOTHING',
      [req.userId, user.id]
    );
    
    const profile = await formatProfile(user, req.userId);
    res.json(profile);
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Unfollow user
app.delete('/api/profiles/:username/follow', authenticateToken, async (req, res) => {
  try {
    const user = await getUserByUsername(req.params.username);
    
    if (!user) {
      return res.status(404).json({ errors: { body: ['User not found'] } });
    }
    
    await pool.query(
      'DELETE FROM follows WHERE follower_id = $1 AND following_id = $2',
      [req.userId, user.id]
    );
    
    const profile = await formatProfile(user, req.userId);
    res.json(profile);
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// List articles
app.get('/api/articles', optionalAuth, async (req, res) => {
  try {
    const { tag, author, favorited, limit = 20, offset = 0 } = req.query;
    
    let query = `
      SELECT DISTINCT a.* FROM articles a
      LEFT JOIN article_tags at ON a.id = at.article_id
      LEFT JOIN users u ON a.author_id = u.id
      LEFT JOIN favorites f ON a.id = f.article_id
      LEFT JOIN users fu ON f.user_id = fu.id
      WHERE 1=1
    `;
    const params = [];
    let paramCount = 1;
    
    if (tag) {
      query += ` AND at.tag = $${paramCount++}`;
      params.push(tag);
    }
    
    if (author) {
      query += ` AND u.username = $${paramCount++}`;
      params.push(author);
    }
    
    if (favorited) {
      query += ` AND fu.username = $${paramCount++}`;
      params.push(favorited);
    }
    
    query += ` ORDER BY a.created_at DESC LIMIT $${paramCount++} OFFSET $${paramCount++}`;
    params.push(limit, offset);
    
    const result = await pool.query(query, params);
    
    const articles = await Promise.all(
      result.rows.map(article => formatArticle(article, req.userId))
    );
    
    res.json({
      articles,
      articlesCount: articles.length
    });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Feed articles
app.get('/api/articles/feed', authenticateToken, async (req, res) => {
  try {
    const { limit = 20, offset = 0 } = req.query;
    
    const query = `
      SELECT a.* FROM articles a
      JOIN follows f ON a.author_id = f.following_id
      WHERE f.follower_id = $1
      ORDER BY a.created_at DESC
      LIMIT $2 OFFSET $3
    `;
    
    const result = await pool.query(query, [req.userId, limit, offset]);
    
    const articles = await Promise.all(
      result.rows.map(article => formatArticle(article, req.userId))
    );
    
    res.json({
      articles,
      articlesCount: articles.length
    });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Get article
app.get('/api/articles/:slug', optionalAuth, async (req, res) => {
  try {
    const result = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (result.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const article = await formatArticle(result.rows[0], req.userId);
    res.json({ article });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Create article
app.post('/api/articles', authenticateToken, async (req, res) => {
  try {
    const { article } = req.body;
    const { title, description, body, tagList = [] } = article;
    
    if (!title || !description || !body) {
      return res.status(422).json({ errors: { body: ['Missing required fields'] } });
    }
    
    let slug = slugify(title);
    
    // Check if slug exists and make it unique
    const existingSlug = await pool.query('SELECT id FROM articles WHERE slug = $1', [slug]);
    if (existingSlug.rows.length > 0) {
      slug = `${slug}-${Date.now()}`;
    }
    
    const result = await pool.query(
      'INSERT INTO articles (slug, title, description, body, author_id) VALUES ($1, $2, $3, $4, $5) RETURNING *',
      [slug, title, description, body, req.userId]
    );
    
    const newArticle = result.rows[0];
    
    // Insert tags
    for (const tag of tagList) {
      await pool.query(
        'INSERT INTO article_tags (article_id, tag) VALUES ($1, $2)',
        [newArticle.id, tag]
      );
    }
    
    const formattedArticle = await formatArticle(newArticle, req.userId);
    res.json({ article: formattedArticle });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Update article
app.put('/api/articles/:slug', authenticateToken, async (req, res) => {
  try {
    const { article } = req.body;
    
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const existingArticle = articleResult.rows[0];
    
    if (existingArticle.author_id !== req.userId) {
      return res.status(403).json({ errors: { body: ['Forbidden'] } });
    }
    
    const updates = ['updated_at = CURRENT_TIMESTAMP'];
    const values = [];
    let paramCount = 1;
    
    if (article.title !== undefined) {
      updates.push(`title = $${paramCount++}`);
      values.push(article.title);
      
      let newSlug = slugify(article.title);
      // Check if slug exists and make it unique (but not if it's the same article)
      const slugCheck = await pool.query('SELECT id FROM articles WHERE slug = $1 AND id != $2', [newSlug, existingArticle.id]);
      if (slugCheck.rows.length > 0) {
        newSlug = `${newSlug}-${Date.now()}`;
      }
      
      updates.push(`slug = $${paramCount++}`);
      values.push(newSlug);
    }
    if (article.description !== undefined) {
      updates.push(`description = $${paramCount++}`);
      values.push(article.description);
    }
    if (article.body !== undefined) {
      updates.push(`body = $${paramCount++}`);
      values.push(article.body);
    }
    
    values.push(req.params.slug);
    
    const result = await pool.query(
      `UPDATE articles SET ${updates.join(', ')} WHERE slug = $${paramCount} RETURNING *`,
      values
    );
    
    const updatedArticle = await formatArticle(result.rows[0], req.userId);
    res.json({ article: updatedArticle });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Delete article
app.delete('/api/articles/:slug', authenticateToken, async (req, res) => {
  try {
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const article = articleResult.rows[0];
    
    if (article.author_id !== req.userId) {
      return res.status(403).json({ errors: { body: ['Forbidden'] } });
    }
    
    await pool.query('DELETE FROM articles WHERE slug = $1', [req.params.slug]);
    
    res.status(200).json({});
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Add comment to article
app.post('/api/articles/:slug/comments', authenticateToken, async (req, res) => {
  try {
    const { comment } = req.body;
    const { body } = comment;
    
    if (!body) {
      return res.status(422).json({ errors: { body: ['Missing required fields'] } });
    }
    
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const article = articleResult.rows[0];
    
    const result = await pool.query(
      'INSERT INTO comments (body, article_id, author_id) VALUES ($1, $2, $3) RETURNING *',
      [body, article.id, req.userId]
    );
    
    const newComment = await formatComment(result.rows[0], req.userId);
    res.json({ comment: newComment });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Get comments from article
app.get('/api/articles/:slug/comments', optionalAuth, async (req, res) => {
  try {
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const article = articleResult.rows[0];
    
    const result = await pool.query(
      'SELECT * FROM comments WHERE article_id = $1 ORDER BY created_at DESC',
      [article.id]
    );
    
    const comments = await Promise.all(
      result.rows.map(comment => formatComment(comment, req.userId))
    );
    
    res.json({ comments });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Delete comment
app.delete('/api/articles/:slug/comments/:id', authenticateToken, async (req, res) => {
  try {
    const commentId = parseInt(req.params.id);
    
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const commentResult = await pool.query('SELECT * FROM comments WHERE id = $1', [commentId]);
    
    if (commentResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Comment not found'] } });
    }
    
    const comment = commentResult.rows[0];
    
    if (comment.author_id !== req.userId) {
      return res.status(403).json({ errors: { body: ['Forbidden'] } });
    }
    
    await pool.query('DELETE FROM comments WHERE id = $1', [commentId]);
    
    res.status(200).json({});
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Favorite article
app.post('/api/articles/:slug/favorite', authenticateToken, async (req, res) => {
  try {
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const article = articleResult.rows[0];
    
    await pool.query(
      'INSERT INTO favorites (user_id, article_id) VALUES ($1, $2) ON CONFLICT DO NOTHING',
      [req.userId, article.id]
    );
    
    const formattedArticle = await formatArticle(article, req.userId);
    res.json({ article: formattedArticle });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Unfavorite article
app.delete('/api/articles/:slug/favorite', authenticateToken, async (req, res) => {
  try {
    const articleResult = await pool.query('SELECT * FROM articles WHERE slug = $1', [req.params.slug]);
    
    if (articleResult.rows.length === 0) {
      return res.status(404).json({ errors: { body: ['Article not found'] } });
    }
    
    const article = articleResult.rows[0];
    
    await pool.query(
      'DELETE FROM favorites WHERE user_id = $1 AND article_id = $2',
      [req.userId, article.id]
    );
    
    const formattedArticle = await formatArticle(article, req.userId);
    res.json({ article: formattedArticle });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Get tags
app.get('/api/tags', async (req, res) => {
  try {
    const result = await pool.query('SELECT DISTINCT tag FROM article_tags ORDER BY tag');
    const tags = result.rows.map(row => row.tag);
    res.json({ tags });
  } catch (err) {
    res.status(422).json({ errors: { body: [err.message] } });
  }
});

// Initialize and start server
initDB().then(() => {
  app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
  });
}).catch(err => {
  console.error('Failed to initialize database:', err);
  process.exit(1);
});
