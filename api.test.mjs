#!/usr/bin/env -S node --test
import assert from "assert";
import { describe, it, afterEach } from "node:test";
import http from "node:http";

// Configuration
const PORT = process.env.PORT || 3000;
const BASE_URL = `http://localhost:${PORT}`;

// Bail on first failure
afterEach((t) => {
  if (!t.passed) {
    console.error(`Bailing because test failed: [${t.name}], with error: ${t.error}`);
    process.exit(1);
  }
});

// ============================================================================
// Test Data Generators
// ============================================================================
let testUserCounter = 0;
let testArticleCounter = 0;

function generateUser() {
  testUserCounter++;
  const randomStr = Math.random().toString(36).slice(2);
  const username = `testuser${testUserCounter}-${randomStr}`;
  return {
    username,
    email: `${username}@example.com`,
    password: `password${testUserCounter}`,
  };
}

function generateArticle(author = "testuser1") {
  testArticleCounter++;
  return {
    title: `Test Article ${testArticleCounter}`,
    description: `Description for article ${testArticleCounter}`,
    body: `This is the body of test article ${testArticleCounter}. It contains multiple sentences to test various scenarios.`,
    tagList: [
      `tag${testArticleCounter}`,
      `test`,
      `article`,
    ],
  };
}

// ============================================================================
// Test Suite
// ============================================================================
describe("RealWorld API Tests", () => {
  let testUsers = [];
  let testArticles = [];
  let testTokens = {};

  // ========================================================================
  // Health Check Tests
  // ========================================================================
  describe("Health Check", () => {
    it("should return 200 OK for GET /", async () => {
      const response = await makeRequest("GET", "/");
      assert.strictEqual(response.status, 200, "Health check should return 200");
    });
  });

  // ========================================================================
  // Registration Tests
  // ========================================================================
  describe("User Registration (POST /api/users)", () => {
    it("should register new user successfully", async () => {
      const user = generateUser();
      const response = await makeRequest("POST", "/api/users", { user });

      assert.strictEqual(response.status, 200, "Registration should return 200");
      assert(response.body.user, "Response should contain user object");
      assert.strictEqual(response.body.user.username, user.username);
      assert.strictEqual(response.body.user.email, user.email);
      assert(response.body.user.token, "Response should contain JWT token");

      testUsers.push(user);
      testTokens[user.email] = response.body.user.token;
    });

    it("should register 20 unique users", async () => {
      for (let i = 0; i < 20; i++) {
        const user = generateUser();
        const response = await makeRequest("POST", "/api/users", { user });

        assert.strictEqual(
          response.status,
          200,
          `User ${i + 1} registration should succeed`
        );
        assert(response.body.user.token);
        testUsers.push(user);
        testTokens[user.email] = response.body.user.token;
      }
    });

    it("should return 422 for missing email", async () => {
      const user = { username: "testuser", password: "password" };
      const response = await makeRequest("POST", "/api/users", { user });

      assert.strictEqual(response.status, 422, "Should return 422 for validation error");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for missing username", async () => {
      const user = { email: "test@example.com", password: "password" };
      const response = await makeRequest("POST", "/api/users", { user });

      assert.strictEqual(response.status, 422, "Should return 422 for validation error");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for missing password", async () => {
      const user = { username: "testuser", email: "test@example.com" };
      const response = await makeRequest("POST", "/api/users", { user });

      assert.strictEqual(response.status, 422, "Should return 422 for validation error");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for duplicate email", async () => {
      const user = testUsers[0];
      const response = await makeRequest("POST", "/api/users", { user });

      assert.strictEqual(response.status, 422, "Should return 422 for duplicate email");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for duplicate username", async () => {
      const user = {
        username: testUsers[0].username,
        email: "newemail@example.com",
        password: "password",
      };
      const response = await makeRequest("POST", "/api/users", { user });

      assert.strictEqual(response.status, 422, "Should return 422 for duplicate username");
      assert(response.body.errors, "Response should contain errors");
    });
  });

  // ========================================================================
  // Login Tests
  // ========================================================================
  describe("User Login (POST /api/users/login)", () => {
    it("should login successfully with correct credentials", async () => {
      const user = testUsers[0];
      const response = await makeRequest("POST", "/api/users/login", {
        user: { email: user.email, password: user.password },
      });

      assert.strictEqual(response.status, 200, "Login should return 200");
      assert(response.body.user, "Response should contain user object");
      assert.strictEqual(response.body.user.email, user.email);
      assert(response.body.user.token, "Response should contain token");
    });

    it("should return 422 for missing email", async () => {
      const response = await makeRequest("POST", "/api/users/login", {
        user: { password: "password" },
      });

      assert.strictEqual(response.status, 422, "Should return 422 for missing email");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for missing password", async () => {
      const response = await makeRequest("POST", "/api/users/login", {
        user: { email: "test@example.com" },
      });

      assert.strictEqual(response.status, 422, "Should return 422 for missing password");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 401 for incorrect password", async () => {
      const user = testUsers[0];
      const response = await makeRequest("POST", "/api/users/login", {
        user: { email: user.email, password: "wrongpassword" },
      });

      assert.strictEqual(response.status, 401, "Should return 401 for wrong password");
    });

    it("should return 401 for non-existent email", async () => {
      const response = await makeRequest("POST", "/api/users/login", {
        user: { email: "nonexistent@example.com", password: "password" },
      });

      assert.strictEqual(response.status, 401, "Should return 401 for non-existent user");
    });
  });

  // ========================================================================
  // Get Current User Tests
  // ========================================================================
  describe("Get Current User (GET /api/user)", () => {
    it("should get current user when authenticated", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const response = await makeRequest("GET", "/api/user", null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.user, "Response should contain user object");
      assert.strictEqual(response.body.user.email, user.email);
    });

    it("should return 401 when not authenticated", async () => {
      const response = await makeRequest("GET", "/api/user");

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 401 with invalid token", async () => {
      const response = await makeRequest("GET", "/api/user", null, "invalid.token.here");

      assert.strictEqual(response.status, 401, "Should return 401 with invalid token");
    });
  });

  // ========================================================================
  // Update User Tests
  // ========================================================================
  describe("Update User (PUT /api/user)", () => {
    it("should update user profile when authenticated", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const updates = {
        user: {
          bio: "I work at statefarm",
          image: "https://i.stack.imgur.com/xHWG8.jpg",
        },
      };

      const response = await makeRequest("PUT", "/api/user", updates, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert.strictEqual(response.body.user.bio, updates.user.bio);
      assert.strictEqual(response.body.user.image, updates.user.image);
    });

    it("should update email when authenticated", async () => {
      const user = testUsers[1];
      const token = testTokens[user.email];
      const newEmail = `updated${Date.now()}@example.com`;
      const updates = { user: { email: newEmail } };

      const response = await makeRequest("PUT", "/api/user", updates, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert.strictEqual(response.body.user.email, newEmail);
      testTokens[newEmail] = token;
    });

    it("should return 401 when not authenticated", async () => {
      const updates = { user: { bio: "New bio" } };
      const response = await makeRequest("PUT", "/api/user", updates);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 422 for invalid email format", async () => {
      const user = testUsers[2];
      const token = testTokens[user.email];
      const updates = { user: { email: "invalid-email" } };

      const response = await makeRequest("PUT", "/api/user", updates, token);

      assert.strictEqual(response.status, 422, "Should return 422 for invalid email");
    });
  });

  // ========================================================================
  // Article Tests
  // ========================================================================
  describe("Create Article (POST /api/articles)", () => {
    it("should create article when authenticated", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const article = generateArticle();

      const response = await makeRequest("POST", "/api/articles", { article }, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.article, "Response should contain article");
      assert(response.body.article.slug, "Article should have slug");
      assert.strictEqual(response.body.article.title, article.title);
      assert.strictEqual(response.body.article.author.username, user.username);

      testArticles.push({
        ...article,
        slug: response.body.article.slug,
        author: user.username,
      });
    });

    it("should create 20 articles by different users", async () => {
      for (let i = 0; i < 20; i++) {
        const user = testUsers[i % testUsers.length];
        const token = testTokens[user.email];
        const article = generateArticle();

        const response = await makeRequest("POST", "/api/articles", { article }, token);

        assert.strictEqual(response.status, 200, `Article ${i + 1} creation should succeed`);
        testArticles.push({
          ...article,
          slug: response.body.article.slug,
          author: user.username,
        });
      }
    });

    it("should return 401 when not authenticated", async () => {
      const article = generateArticle();
      const response = await makeRequest("POST", "/api/articles", { article });

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 422 for missing title", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const article = { description: "desc", body: "body" };

      const response = await makeRequest("POST", "/api/articles", { article }, token);

      assert.strictEqual(response.status, 422, "Should return 422 for missing title");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for missing description", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const article = { title: "title", body: "body" };

      const response = await makeRequest("POST", "/api/articles", { article }, token);

      assert.strictEqual(response.status, 422, "Should return 422 for missing description");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 422 for missing body", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const article = { title: "title", description: "desc" };

      const response = await makeRequest("POST", "/api/articles", { article }, token);

      assert.strictEqual(response.status, 422, "Should return 422 for missing body");
      assert(response.body.errors, "Response should contain errors");
    });
  });

  // ========================================================================
  // List Articles Tests
  // ========================================================================
  describe("List Articles (GET /api/articles)", () => {
    it("should list articles without authentication", async () => {
      const response = await makeRequest("GET", "/api/articles");

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.articles), "Should return articles array");
      assert(response.body.articlesCount >= 0, "Should return articlesCount");
    });

    it("should list articles with limit parameter", async () => {
      const response = await makeRequest("GET", "/api/articles?limit=5");

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.articles.length <= 5, "Should respect limit parameter");
    });

    it("should list articles with offset parameter", async () => {
      const response = await makeRequest("GET", "/api/articles?offset=10");

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.articles), "Should return articles array");
    });

    it("should filter articles by tag", async () => {
      const response = await makeRequest("GET", "/api/articles?tag=test");

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.articles), "Should return articles array");
    });

    it("should filter articles by author", async () => {
      const author = testUsers[0].username;
      const response = await makeRequest("GET", `/api/articles?author=${author}`);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.articles), "Should return articles array");
    });

    it("should filter articles by favorited user", async () => {
      const user = testUsers[0].username;
      const response = await makeRequest("GET", `/api/articles?favorited=${user}`);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.articles), "Should return articles array");
    });
  });

  // ========================================================================
  // Get Article Tests
  // ========================================================================
  describe("Get Article (GET /api/articles/:slug)", () => {
    it("should get article by slug", async () => {
      const article = testArticles[0];
      const response = await makeRequest("GET", `/api/articles/${article.slug}`);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.article, "Response should contain article");
      assert.strictEqual(response.body.article.slug, article.slug);
      assert.strictEqual(response.body.article.title, article.title);
    });

    it("should return 404 for non-existent article", async () => {
      const response = await makeRequest("GET", "/api/articles/non-existent-article-slug");

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });

    it("should show favorited status for authenticated user", async () => {
      const article = testArticles[0];
      const user = testUsers[0];
      const token = testTokens[user.email];
      const response = await makeRequest(
        "GET",
        `/api/articles/${article.slug}`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert.strictEqual(typeof response.body.article.favorited, "boolean");
    });
  });

  // ========================================================================
  // Update Article Tests
  // ========================================================================
  describe("Update Article (PUT /api/articles/:slug)", () => {
    it("should update article by author", async () => {
      const article = testArticles[0];
      const user = testUsers[0];
      const token = testTokens[user.email];
      const updates = {
        article: {
          title: `Updated ${article.title}`,
          description: "Updated description",
        },
      };

      const response = await makeRequest(
        "PUT",
        `/api/articles/${article.slug}`,
        updates,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert.strictEqual(response.body.article.title, updates.article.title);

      // Update slug in testArticles because title changed
      testArticles[0].slug = response.body.article.slug;
    });

    it("should return 403 when updating article by non-author", async () => {
      const article = testArticles[0];
      const otherUser = testUsers[1];
      const token = testTokens[otherUser.email];
      const updates = { article: { title: "Hacked title" } };

      const response = await makeRequest(
        "PUT",
        `/api/articles/${article.slug}`,
        updates,
        token
      );

      assert.strictEqual(response.status, 403, "Should return 403 for non-author");
    });

    it("should return 401 when updating article without authentication", async () => {
      const article = testArticles[0];
      const updates = { article: { title: "Updated title" } };

      const response = await makeRequest("PUT", `/api/articles/${article.slug}`, updates);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent article", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const updates = { article: { title: "Updated title" } };

      const response = await makeRequest(
        "PUT",
        "/api/articles/non-existent-slug",
        updates,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Delete Article Tests
  // ========================================================================
  describe("Delete Article (DELETE /api/articles/:slug)", () => {
    it("should delete article by author", async () => {
      const user = testUsers[2];
      const token = testTokens[user.email];
      const article = generateArticle();

      // First create an article
      const createRes = await makeRequest("POST", "/api/articles", { article }, token);
      const slug = createRes.body.article.slug;

      // Then delete it
      const response = await makeRequest("DELETE", `/api/articles/${slug}`, null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
    });

    it("should return 403 when deleting article by non-author", async () => {
      const article = testArticles[0];
      const otherUser = testUsers[1];
      const token = testTokens[otherUser.email];

      const response = await makeRequest("DELETE", `/api/articles/${article.slug}`, null, token);

      assert.strictEqual(response.status, 403, "Should return 403 for non-author");
    });

    it("should return 401 when deleting article without authentication", async () => {
      const article = testArticles[0];
      const response = await makeRequest("DELETE", `/api/articles/${article.slug}`);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent article", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        "/api/articles/non-existent-slug",
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Article Feed Tests
  // ========================================================================
  describe("Article Feed (GET /api/articles/feed)", () => {
    it("should return feed for authenticated user", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const response = await makeRequest("GET", "/api/articles/feed", null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.articles), "Should return articles array");
      assert(response.body.articlesCount >= 0, "Should return articlesCount");
    });

    it("should return 401 when not authenticated", async () => {
      const response = await makeRequest("GET", "/api/articles/feed");

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should support limit parameter", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const response = await makeRequest("GET", "/api/articles/feed?limit=5", null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.articles.length <= 5, "Should respect limit");
    });

    it("should support offset parameter", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const response = await makeRequest("GET", "/api/articles/feed?offset=0", null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
    });
  });

  // ========================================================================
  // Favorite Article Tests
  // ========================================================================
  describe("Favorite Article (POST /api/articles/:slug/favorite)", () => {
    it("should favorite article when authenticated", async () => {
      const article = testArticles[0];
      const user = testUsers[2];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "POST",
        `/api/articles/${article.slug}/favorite`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.article, "Response should contain article");
      assert.strictEqual(response.body.article.favorited, true, "Article should be favorited");
      assert(response.body.article.favoritesCount > 0, "Favorites count should increase");
    });

    it("should return 401 when not authenticated", async () => {
      const article = testArticles[0];
      const response = await makeRequest("POST", `/api/articles/${article.slug}/favorite`);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent article", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "POST",
        "/api/articles/non-existent-slug/favorite",
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Unfavorite Article Tests
  // ========================================================================
  describe("Unfavorite Article (DELETE /api/articles/:slug/favorite)", () => {
    it("should unfavorite article when authenticated", async () => {
      const article = testArticles[0];
      const user = testUsers[2];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        `/api/articles/${article.slug}/favorite`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.article, "Response should contain article");
      assert.strictEqual(response.body.article.favorited, false, "Article should not be favorited");
    });

    it("should return 401 when not authenticated", async () => {
      const article = testArticles[0];
      const response = await makeRequest("DELETE", `/api/articles/${article.slug}/favorite`);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent article", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        "/api/articles/non-existent-slug/favorite",
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Comment Tests
  // ========================================================================
  describe("Add Comment (POST /api/articles/:slug/comments)", () => {
    it("should add comment to article when authenticated", async () => {
      const article = testArticles[0];
      const user = testUsers[0];
      const token = testTokens[user.email];
      const comment = { comment: { body: "Great article!" } };

      const response = await makeRequest(
        "POST",
        `/api/articles/${article.slug}/comments`,
        comment,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.comment, "Response should contain comment");
      assert.strictEqual(response.body.comment.body, comment.comment.body);
      assert.strictEqual(response.body.comment.author.username, user.username);
      assert(response.body.comment.id, "Comment should have id");
    });

    it("should add 20 comments to various articles", async () => {
      for (let i = 0; i < 20; i++) {
        const article = testArticles[i % testArticles.length];
        const user = testUsers[i % testUsers.length];
        const token = testTokens[user.email];
        const comment = {
          comment: { body: `Comment number ${i + 1} on article ${article.slug}` },
        };

        const response = await makeRequest(
          "POST",
          `/api/articles/${article.slug}/comments`,
          comment,
          token
        );

        assert.strictEqual(response.status, 200, `Comment ${i + 1} should be created`);
      }
    });

    it("should return 401 when not authenticated", async () => {
      const article = testArticles[0];
      const comment = { comment: { body: "Great article!" } };

      const response = await makeRequest("POST", `/api/articles/${article.slug}/comments`, comment);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 422 for missing comment body", async () => {
      const article = testArticles[0];
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "POST",
        `/api/articles/${article.slug}/comments`,
        { comment: {} },
        token
      );

      assert.strictEqual(response.status, 422, "Should return 422 for missing body");
      assert(response.body.errors, "Response should contain errors");
    });

    it("should return 404 for non-existent article", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const comment = { comment: { body: "Great article!" } };

      const response = await makeRequest(
        "POST",
        "/api/articles/non-existent-slug/comments",
        comment,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Get Comments Tests
  // ========================================================================
  describe("Get Comments (GET /api/articles/:slug/comments)", () => {
    it("should get comments from article without authentication", async () => {
      const article = testArticles[0];
      const response = await makeRequest("GET", `/api/articles/${article.slug}/comments`);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.comments), "Should return comments array");
    });

    it("should get comments with authentication", async () => {
      const article = testArticles[0];
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "GET",
        `/api/articles/${article.slug}/comments`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.comments), "Should return comments array");
    });

    it("should return 404 for non-existent article", async () => {
      const response = await makeRequest("GET", "/api/articles/non-existent-slug/comments");

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Delete Comment Tests
  // ========================================================================
  describe("Delete Comment (DELETE /api/articles/:slug/comments/:id)", () => {
    it("should delete own comment", async () => {
      const article = testArticles[1];
      const user = testUsers[3];
      const token = testTokens[user.email];

      // First add a comment
      const createRes = await makeRequest(
        "POST",
        `/api/articles/${article.slug}/comments`,
        { comment: { body: "My comment to delete" } },
        token
      );

      const commentId = createRes.body.comment.id;

      // Then delete it
      const response = await makeRequest(
        "DELETE",
        `/api/articles/${article.slug}/comments/${commentId}`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
    });

    it("should return 403 when deleting other user's comment", async () => {
      const article = testArticles[0];
      const user1 = testUsers[4];
      const user2 = testUsers[5];
      const token1 = testTokens[user1.email];
      const token2 = testTokens[user2.email];

      // User1 adds a comment
      const createRes = await makeRequest(
        "POST",
        `/api/articles/${article.slug}/comments`,
        { comment: { body: "User1 comment" } },
        token1
      );

      const commentId = createRes.body.comment.id;

      // User2 tries to delete it
      const response = await makeRequest(
        "DELETE",
        `/api/articles/${article.slug}/comments/${commentId}`,
        null,
        token2
      );

      assert.strictEqual(response.status, 403, "Should return 403 for non-author");
    });

    it("should return 401 when not authenticated", async () => {
      const article = testArticles[0];
      const response = await makeRequest(
        "DELETE",
        `/api/articles/${article.slug}/comments/1`
      );

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent comment", async () => {
      const article = testArticles[0];
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        `/api/articles/${article.slug}/comments/999999`,
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent comment");
    });

    it("should return 404 for non-existent article", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        "/api/articles/non-existent-slug/comments/1",
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent article");
    });
  });

  // ========================================================================
  // Profile Tests
  // ========================================================================
  describe("Get Profile (GET /api/profiles/:username)", () => {
    it("should get profile without authentication", async () => {
      const username = testUsers[0].username;
      const response = await makeRequest("GET", `/api/profiles/${username}`);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.profile, "Response should contain profile");
      assert.strictEqual(response.body.profile.username, username);
      assert.strictEqual(response.body.profile.following, false);
    });

    it("should get profile with authentication", async () => {
      const username = testUsers[0].username;
      const user = testUsers[1];
      const token = testTokens[user.email];

      const response = await makeRequest("GET", `/api/profiles/${username}`, null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.profile, "Response should contain profile");
      assert.strictEqual(response.body.profile.username, username);
      assert.strictEqual(typeof response.body.profile.following, "boolean");
    });

    it("should return 404 for non-existent user", async () => {
      const response = await makeRequest("GET", "/api/profiles/nonexistentuser");

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent user");
    });
  });

  // ========================================================================
  // Follow Tests
  // ========================================================================
  describe("Follow User (POST /api/profiles/:username/follow)", () => {
    it("should follow user when authenticated", async () => {
      const targetUser = testUsers[10];
      const user = testUsers[11];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "POST",
        `/api/profiles/${targetUser.username}/follow`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.profile, "Response should contain profile");
      assert.strictEqual(response.body.profile.following, true, "Profile should show following");
    });

    it("should return 401 when not authenticated", async () => {
      const targetUser = testUsers[0];
      const response = await makeRequest("POST", `/api/profiles/${targetUser.username}/follow`);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent user", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "POST",
        "/api/profiles/nonexistentuser/follow",
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent user");
    });

    it("should not error when following already followed user", async () => {
      const targetUser = testUsers[10];
      const user = testUsers[11];
      const token = testTokens[user.email];

      // Follow first time (already done in previous test)
      // Follow second time
      const response = await makeRequest(
        "POST",
        `/api/profiles/${targetUser.username}/follow`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
    });
  });

  // ========================================================================
  // Unfollow Tests
  // ========================================================================
  describe("Unfollow User (DELETE /api/profiles/:username/follow)", () => {
    it("should unfollow user when authenticated", async () => {
      const targetUser = testUsers[12];
      const user = testUsers[13];
      const token = testTokens[user.email];

      // First follow the user
      await makeRequest("POST", `/api/profiles/${targetUser.username}/follow`, null, token);

      // Then unfollow
      const response = await makeRequest(
        "DELETE",
        `/api/profiles/${targetUser.username}/follow`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.profile, "Response should contain profile");
      assert.strictEqual(response.body.profile.following, false, "Profile should show not following");
    });

    it("should return 401 when not authenticated", async () => {
      const targetUser = testUsers[0];
      const response = await makeRequest("DELETE", `/api/profiles/${targetUser.username}/follow`);

      assert.strictEqual(response.status, 401, "Should return 401 without token");
    });

    it("should return 404 for non-existent user", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        "/api/profiles/nonexistentuser/follow",
        null,
        token
      );

      assert.strictEqual(response.status, 404, "Should return 404 for non-existent user");
    });

    it("should not error when unfollowing non-followed user", async () => {
      const targetUser = testUsers[14];
      const user = testUsers[15];
      const token = testTokens[user.email];

      const response = await makeRequest(
        "DELETE",
        `/api/profiles/${targetUser.username}/follow`,
        null,
        token
      );

      assert.strictEqual(response.status, 200, "Should return 200");
    });
  });

  // ========================================================================
  // Tags Tests
  // ========================================================================
  describe("Get Tags (GET /api/tags)", () => {
    it("should get list of tags without authentication", async () => {
      const response = await makeRequest("GET", "/api/tags");

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.tags), "Response should contain tags array");
      assert(response.body.tags.length > 0, "Should have at least some tags");
    });

    it("should get list of tags with authentication", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const response = await makeRequest("GET", "/api/tags", null, token);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Array.isArray(response.body.tags), "Response should contain tags array");
    });

    it("tags should be strings", async () => {
      const response = await makeRequest("GET", "/api/tags");

      assert.strictEqual(response.status, 200, "Should return 200");
      for (const tag of response.body.tags) {
        assert.strictEqual(typeof tag, "string", "Each tag should be a string");
      }
    });
  });

  // ========================================================================
  // Edge Cases and Validation Tests
  // ========================================================================
  describe("Edge Cases and Validation", () => {
    it("should handle empty article list gracefully", async () => {
      const response = await makeRequest("GET", "/api/articles");

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(Number.isInteger(response.body.articlesCount), "articlesCount should be integer");
    });

    it("should handle special characters in article slug", async () => {
      // Articles with special chars in title should generate safe slugs
      const user = testUsers[0];
      const token = testTokens[user.email];
      const article = {
        title: "Article with special chars!@#$%",
        description: "Test description",
        body: "Test body",
      };

      const response = await makeRequest("POST", "/api/articles", { article }, token);

      assert.strictEqual(response.status, 200, "Should create article");
      assert(response.body.article.slug, "Should have a slug");
      // Slug should be URL-safe
      assert(/^[a-z0-9\-]+$/.test(response.body.article.slug), "Slug should be URL-safe");
    });

    it("should handle concurrent requests properly", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];

      const promises = Array(5)
        .fill(null)
        .map(() =>
          makeRequest("GET", `/api/user`, null, token)
        );

      const responses = await Promise.all(promises);

      for (const response of responses) {
        assert.strictEqual(response.status, 200, "All concurrent requests should succeed");
      }
    });

    it("should handle very long article body", async () => {
      const user = testUsers[0];
      const token = testTokens[user.email];
      const longBody = "A".repeat(10000);

      const article = {
        title: "Long article",
        description: "Long description",
        body: longBody,
      };

      const response = await makeRequest("POST", "/api/articles", { article }, token);

      assert.strictEqual(response.status, 200, "Should handle long body");
      assert.strictEqual(response.body.article.body, longBody, "Body should be preserved");
    });

    it("should preserve article metadata timestamps", async () => {
      const article = testArticles[0];
      const response = await makeRequest("GET", `/api/articles/${article.slug}`);

      assert.strictEqual(response.status, 200, "Should return 200");
      assert(response.body.article.createdAt, "Should have createdAt");
      assert(response.body.article.updatedAt, "Should have updatedAt");
      // Verify they're ISO date strings
      const createdDate = new Date(response.body.article.createdAt);
      assert(!isNaN(createdDate.getTime()), "createdAt should be valid ISO date");
    });
  });
});

// ============================================================================
// HTTP Request Helper
// ============================================================================
function makeRequest(method, path, body = null, token = null) {
  return new Promise((resolve, reject) => {
    const url = new URL(BASE_URL + path);
    const options = {
      method,
      hostname: url.hostname,
      port: url.port || 80,
      path: url.pathname + url.search,
      headers: {
        "Content-Type": "application/json",
      },
    };

    if (token) {
      options.headers.Authorization = token;
    }

    const req = http.request(options, (res) => {
      let data = "";
      res.on("data", (chunk) => {
        data += chunk;
      });
      res.on("end", () => {
        const response = {
          status: res.statusCode,
          headers: res.headers,
          body: data ? JSON.parse(data) : null,
        };
        resolve(response);
      });
    });

    req.on("error", reject);

    if (body) {
      req.write(JSON.stringify(body));
    }
    req.end();
  });
}
