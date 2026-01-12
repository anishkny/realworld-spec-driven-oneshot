#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>
#include "../utils/Auth.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <numeric>

using namespace drogon;
using namespace orm;

class ArticleController : public HttpController<ArticleController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ArticleController::listArticles, "/api/articles", Get);
    ADD_METHOD_TO(ArticleController::feedArticles, "/api/articles/feed", Get);
    ADD_METHOD_TO(ArticleController::getArticle, "/api/articles/{slug}", Get);
    ADD_METHOD_TO(ArticleController::createArticle, "/api/articles", Post);
    ADD_METHOD_TO(ArticleController::updateArticle, "/api/articles/{slug}", Put);
    ADD_METHOD_TO(ArticleController::deleteArticle, "/api/articles/{slug}", Delete);
    ADD_METHOD_TO(ArticleController::favoriteArticle, "/api/articles/{slug}/favorite", Post);
    ADD_METHOD_TO(ArticleController::unfavoriteArticle, "/api/articles/{slug}/favorite", Delete);
    METHOD_LIST_END

    void listArticles(const HttpRequestPtr& req,
                     std::function<void (const HttpResponsePtr &)> &&callback)
    {
        int currentUserId = 0;
        std::string currentUsername, currentEmail;
        authenticateRequest(req, currentUserId, currentUsername, currentEmail);

        auto params = req->getParameters();
        std::string tag = params.find("tag") != params.end() ? params.at("tag") : "";
        std::string author = params.find("author") != params.end() ? params.at("author") : "";
        std::string favorited = params.find("favorited") != params.end() ? params.at("favorited") : "";
        int limit = params.find("limit") != params.end() ? std::stoi(params.at("limit")) : 20;
        int offset = params.find("offset") != params.end() ? std::stoi(params.at("offset")) : 0;

        std::string sql = R"(
            SELECT DISTINCT a.id, a.slug, a.title, a.description, a.body,
                   a.created_at, a.updated_at, a.author_id,
                   u.username, u.bio, u.image,
                   CASE WHEN f1.follower_id IS NOT NULL THEN true ELSE false END as following,
                   CASE WHEN f2.user_id IS NOT NULL THEN true ELSE false END as favorited,
                   COALESCE(fav_count.count, 0) as favorites_count
            FROM articles a
            JOIN users u ON a.author_id = u.id
            LEFT JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1
            LEFT JOIN favorites f2 ON a.id = f2.article_id AND f2.user_id = $1
            LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count 
                ON a.id = fav_count.article_id
        )";

        std::vector<std::string> whereClauses;
        std::vector<std::string> queryParams;
        queryParams.push_back(std::to_string(currentUserId));
        int paramCount = 2;

        if (!tag.empty()) {
            whereClauses.push_back("a.id IN (SELECT article_id FROM article_tags WHERE tag = $" + std::to_string(paramCount++) + ")");
            queryParams.push_back(tag);
        }
        if (!author.empty()) {
            whereClauses.push_back("u.username = $" + std::to_string(paramCount++));
            queryParams.push_back(author);
        }
        if (!favorited.empty()) {
            whereClauses.push_back("a.id IN (SELECT f.article_id FROM favorites f JOIN users u2 ON f.user_id = u2.id WHERE u2.username = $" + std::to_string(paramCount++) + ")");
            queryParams.push_back(favorited);
        }

        if (!whereClauses.empty()) {
            sql += " WHERE " + std::accumulate(whereClauses.begin(), whereClauses.end(), std::string(),
                [](const std::string& a, const std::string& b) {
                    return a.empty() ? b : a + " AND " + b;
                });
        }

        sql += " ORDER BY a.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);

        auto dbClient = app().getDbClient("default");
        
        // Execute query with variable number of parameters
        auto handleArticles = [callback, dbClient](const Result& r) {
            Json::Value articlesArray(Json::arrayValue);
            
            if (!r.empty()) {
                std::vector<int> articleIds;
                for (size_t i = 0; i < r.size(); i++) {
                    articleIds.push_back(r[i]["id"].as<int>());
                }
                
                // Get tags for all articles
                std::string tagSql = "SELECT article_id, tag FROM article_tags WHERE article_id = ANY($1)";
                
                std::ostringstream oss;
                oss << "{";
                for (size_t i = 0; i < articleIds.size(); i++) {
                    if (i > 0) oss << ",";
                    oss << articleIds[i];
                }
                oss << "}";
                
                dbClient->execSqlAsync(
                    tagSql,
                    [callback, r, articlesArray](const Result& tagResult) mutable {
                        std::map<int, std::vector<std::string>> articleTags;
                        for (size_t i = 0; i < tagResult.size(); i++) {
                            int articleId = tagResult[i]["article_id"].as<int>();
                            std::string tag = tagResult[i]["tag"].as<std::string>();
                            articleTags[articleId].push_back(tag);
                        }
                        
                        for (size_t i = 0; i < r.size(); i++) {
                            Json::Value article;
                            int articleId = r[i]["id"].as<int>();
                            
                            article["slug"] = r[i]["slug"].as<std::string>();
                            article["title"] = r[i]["title"].as<std::string>();
                            article["description"] = r[i]["description"].as<std::string>();
                            article["body"] = r[i]["body"].as<std::string>();
                            article["createdAt"] = r[i]["created_at"].as<std::string>();
                            article["updatedAt"] = r[i]["updated_at"].as<std::string>();
                            article["favorited"] = r[i]["favorited"].as<bool>();
                            article["favoritesCount"] = r[i]["favorites_count"].as<int>();
                            
                            Json::Value tagList(Json::arrayValue);
                            for (const auto& tag : articleTags[articleId]) {
                                tagList.append(tag);
                            }
                            article["tagList"] = tagList;
                            
                            Json::Value author;
                            author["username"] = r[i]["username"].as<std::string>();
                            author["bio"] = r[i]["bio"].isNull() ? "" : r[i]["bio"].as<std::string>();
                            author["image"] = r[i]["image"].isNull() ? Json::Value::null : r[i]["image"].as<std::string>();
                            author["following"] = r[i]["following"].as<bool>();
                            article["author"] = author;
                            
                            articlesArray.append(article);
                        }
                        
                        Json::Value response;
                        response["articles"] = articlesArray;
                        response["articlesCount"] = static_cast<int>(articlesArray.size());
                        
                        auto resp = HttpResponse::newHttpJsonResponse(response);
                        callback(resp);
                    },
                    [callback](const DrogonDbException& e) {
                        Json::Value error;
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    oss.str()
                );
            } else {
                Json::Value response;
                response["articles"] = articlesArray;
                response["articlesCount"] = 0;
                
                auto resp = HttpResponse::newHttpJsonResponse(response);
                callback(resp);
            }
        };
        
        auto handleError = [callback](const DrogonDbException& e) {
            Json::Value error;
            error["errors"]["body"].append("Database error");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
        };
        
        // Call with appropriate number of parameters
        if (queryParams.size() == 1) {
            dbClient->execSqlAsync(sql, handleArticles, handleError, std::stoi(queryParams[0]));
        } else if (queryParams.size() == 2) {
            dbClient->execSqlAsync(sql, handleArticles, handleError, std::stoi(queryParams[0]), queryParams[1]);
        } else if (queryParams.size() == 3) {
            dbClient->execSqlAsync(sql, handleArticles, handleError, std::stoi(queryParams[0]), queryParams[1], queryParams[2]);
        } else if (queryParams.size() == 4) {
            dbClient->execSqlAsync(sql, handleArticles, handleError, std::stoi(queryParams[0]), queryParams[1], queryParams[2], queryParams[3]);
        }
    }

    void feedArticles(const HttpRequestPtr& req,
                     std::function<void (const HttpResponsePtr &)> &&callback)
    {
        int currentUserId;
        std::string currentUsername, currentEmail;
        
        if (!authenticateRequest(req, currentUserId, currentUsername, currentEmail)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto params = req->getParameters();
        int limit = params.find("limit") != params.end() ? std::stoi(params.at("limit")) : 20;
        int offset = params.find("offset") != params.end() ? std::stoi(params.at("offset")) : 0;

        std::string sql = "SELECT a.id, a.slug, a.title, a.description, a.body, "
                         "a.created_at, a.updated_at, a.author_id, "
                         "u.username, u.bio, u.image, "
                         "true as following, "
                         "CASE WHEN f2.user_id IS NOT NULL THEN true ELSE false END as favorited, "
                         "COALESCE(fav_count.count, 0) as favorites_count "
                         "FROM articles a "
                         "JOIN users u ON a.author_id = u.id "
                         "JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1 "
                         "LEFT JOIN favorites f2 ON a.id = f2.article_id AND f2.user_id = $1 "
                         "LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count "
                         "ON a.id = fav_count.article_id "
                         "ORDER BY a.created_at DESC "
                         "LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            sql,
            [callback, dbClient](const Result& r) {
                Json::Value articlesArray(Json::arrayValue);
                
                if (!r.empty()) {
                    std::vector<int> articleIds;
                    for (size_t i = 0; i < r.size(); i++) {
                        articleIds.push_back(r[i]["id"].as<int>());
                    }
                    
                    std::ostringstream oss;
                    oss << "{";
                    for (size_t i = 0; i < articleIds.size(); i++) {
                        if (i > 0) oss << ",";
                        oss << articleIds[i];
                    }
                    oss << "}";
                    
                    dbClient->execSqlAsync(
                        "SELECT article_id, tag FROM article_tags WHERE article_id = ANY($1)",
                        [callback, r, articlesArray](const Result& tagResult) mutable {
                            std::map<int, std::vector<std::string>> articleTags;
                            for (size_t i = 0; i < tagResult.size(); i++) {
                                int articleId = tagResult[i]["article_id"].as<int>();
                                std::string tag = tagResult[i]["tag"].as<std::string>();
                                articleTags[articleId].push_back(tag);
                            }
                            
                            for (size_t i = 0; i < r.size(); i++) {
                                Json::Value article;
                                int articleId = r[i]["id"].as<int>();
                                
                                article["slug"] = r[i]["slug"].as<std::string>();
                                article["title"] = r[i]["title"].as<std::string>();
                                article["description"] = r[i]["description"].as<std::string>();
                                article["body"] = r[i]["body"].as<std::string>();
                                article["createdAt"] = r[i]["created_at"].as<std::string>();
                                article["updatedAt"] = r[i]["updated_at"].as<std::string>();
                                article["favorited"] = r[i]["favorited"].as<bool>();
                                article["favoritesCount"] = r[i]["favorites_count"].as<int>();
                                
                                Json::Value tagList(Json::arrayValue);
                                for (const auto& tag : articleTags[articleId]) {
                                    tagList.append(tag);
                                }
                                article["tagList"] = tagList;
                                
                                Json::Value author;
                                author["username"] = r[i]["username"].as<std::string>();
                                author["bio"] = r[i]["bio"].isNull() ? "" : r[i]["bio"].as<std::string>();
                                author["image"] = r[i]["image"].isNull() ? Json::Value::null : r[i]["image"].as<std::string>();
                                author["following"] = true;
                                article["author"] = author;
                                
                                articlesArray.append(article);
                            }
                            
                            Json::Value response;
                            response["articles"] = articlesArray;
                            response["articlesCount"] = static_cast<int>(articlesArray.size());
                            
                            auto resp = HttpResponse::newHttpJsonResponse(response);
                            callback(resp);
                        },
                        [callback](const DrogonDbException& e) {
                            Json::Value error;
                            error["errors"]["body"].append("Database error");
                            auto resp = HttpResponse::newHttpJsonResponse(error);
                            resp->setStatusCode(k422UnprocessableEntity);
                            callback(resp);
                        },
                        oss.str()
                    );
                } else {
                    Json::Value response;
                    response["articles"] = articlesArray;
                    response["articlesCount"] = 0;
                    
                    auto resp = HttpResponse::newHttpJsonResponse(response);
                    callback(resp);
                }
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            currentUserId
        );
    }

    void getArticle(const HttpRequestPtr& req,
                   std::function<void (const HttpResponsePtr &)> &&callback,
                   const std::string& slug)
    {
        int currentUserId = 0;
        std::string currentUsername, currentEmail;
        authenticateRequest(req, currentUserId, currentUsername, currentEmail);

        auto dbClient = app().getDbClient("default");
        
        std::string sql = R"(
            SELECT a.id, a.slug, a.title, a.description, a.body,
                   a.created_at, a.updated_at, a.author_id,
                   u.username, u.bio, u.image,
                   CASE WHEN f1.follower_id IS NOT NULL THEN true ELSE false END as following,
                   CASE WHEN f2.user_id IS NOT NULL THEN true ELSE false END as favorited,
                   COALESCE(fav_count.count, 0) as favorites_count
            FROM articles a
            JOIN users u ON a.author_id = u.id
            LEFT JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1
            LEFT JOIN favorites f2 ON a.id = f2.article_id AND f2.user_id = $1
            LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count 
                ON a.id = fav_count.article_id
            WHERE a.slug = $2
        )";
        
        dbClient->execSqlAsync(
            sql,
            [callback, dbClient](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Article not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int articleId = r[0]["id"].as<int>();
                
                dbClient->execSqlAsync(
                    "SELECT tag FROM article_tags WHERE article_id = $1",
                    [callback, r](const Result& tagResult) {
                        Json::Value article;
                        article["slug"] = r[0]["slug"].as<std::string>();
                        article["title"] = r[0]["title"].as<std::string>();
                        article["description"] = r[0]["description"].as<std::string>();
                        article["body"] = r[0]["body"].as<std::string>();
                        article["createdAt"] = r[0]["created_at"].as<std::string>();
                        article["updatedAt"] = r[0]["updated_at"].as<std::string>();
                        article["favorited"] = r[0]["favorited"].as<bool>();
                        article["favoritesCount"] = r[0]["favorites_count"].as<int>();
                        
                        Json::Value tagList(Json::arrayValue);
                        for (size_t i = 0; i < tagResult.size(); i++) {
                            tagList.append(tagResult[i]["tag"].as<std::string>());
                        }
                        article["tagList"] = tagList;
                        
                        Json::Value author;
                        author["username"] = r[0]["username"].as<std::string>();
                        author["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                        author["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                        author["following"] = r[0]["following"].as<bool>();
                        article["author"] = author;
                        
                        Json::Value response;
                        response["article"] = article;
                        
                        auto resp = HttpResponse::newHttpJsonResponse(response);
                        callback(resp);
                    },
                    [callback](const DrogonDbException& e) {
                        Json::Value error;
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    articleId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            currentUserId, slug
        );
    }

    void createArticle(const HttpRequestPtr& req,
                      std::function<void (const HttpResponsePtr &)> &&callback)
    {
        int currentUserId;
        std::string currentUsername, currentEmail;
        
        if (!authenticateRequest(req, currentUserId, currentUsername, currentEmail)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr || !(*jsonPtr).isMember("article")) {
            Json::Value error;
            error["errors"]["body"].append("Invalid request format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto article = (*jsonPtr)["article"];
        
        if (!article.isMember("title") || !article.isMember("description") || !article.isMember("body")) {
            Json::Value error;
            error["errors"]["body"].append("Missing required fields");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        std::string title = article["title"].asString();
        std::string description = article["description"].asString();
        std::string body = article["body"].asString();
        std::string slug = generateSlug(title);
        
        std::vector<std::string> tags;
        if (article.isMember("tagList")) {
            for (const auto& tag : article["tagList"]) {
                tags.push_back(tag.asString());
            }
        }

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "INSERT INTO articles (slug, title, description, body, author_id) VALUES ($1, $2, $3, $4, $5) RETURNING id, created_at, updated_at",
            [callback, dbClient, currentUserId, currentUsername, slug, title, description, body, tags](const Result& r) {
                int articleId = r[0]["id"].as<int>();
                std::string createdAt = r[0]["created_at"].as<std::string>();
                std::string updatedAt = r[0]["updated_at"].as<std::string>();
                
                // Insert tags
                if (!tags.empty()) {
                    for (const auto& tag : tags) {
                        dbClient->execSqlAsync(
                            "INSERT INTO article_tags (article_id, tag) VALUES ($1, $2)",
                            [](const Result& r) {},
                            [](const DrogonDbException& e) {},
                            articleId, tag
                        );
                    }
                }
                
                // Get user info
                dbClient->execSqlAsync(
                    "SELECT username, bio, image FROM users WHERE id = $1",
                    [callback, slug, title, description, body, tags, createdAt, updatedAt](const Result& userResult) {
                        Json::Value response;
                        Json::Value article;
                        
                        article["slug"] = slug;
                        article["title"] = title;
                        article["description"] = description;
                        article["body"] = body;
                        article["createdAt"] = createdAt;
                        article["updatedAt"] = updatedAt;
                        article["favorited"] = false;
                        article["favoritesCount"] = 0;
                        
                        Json::Value tagList(Json::arrayValue);
                        for (const auto& tag : tags) {
                            tagList.append(tag);
                        }
                        article["tagList"] = tagList;
                        
                        Json::Value author;
                        author["username"] = userResult[0]["username"].as<std::string>();
                        author["bio"] = userResult[0]["bio"].isNull() ? "" : userResult[0]["bio"].as<std::string>();
                        author["image"] = userResult[0]["image"].isNull() ? Json::Value::null : userResult[0]["image"].as<std::string>();
                        author["following"] = false;
                        article["author"] = author;
                        
                        response["article"] = article;
                        
                        auto resp = HttpResponse::newHttpJsonResponse(response);
                        callback(resp);
                    },
                    [callback](const DrogonDbException& e) {
                        Json::Value error;
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    currentUserId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Article with this title already exists");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            slug, title, description, body, currentUserId
        );
    }

    void updateArticle(const HttpRequestPtr& req,
                      std::function<void (const HttpResponsePtr &)> &&callback,
                      const std::string& slug)
    {
        int currentUserId;
        std::string currentUsername, currentEmail;
        
        if (!authenticateRequest(req, currentUserId, currentUsername, currentEmail)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr || !(*jsonPtr).isMember("article")) {
            Json::Value error;
            error["errors"]["body"].append("Invalid request format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto dbClient = app().getDbClient("default");
        
        // First check if article exists and user owns it
        dbClient->execSqlAsync(
            "SELECT id, author_id FROM articles WHERE slug = $1",
            [callback, dbClient, currentUserId, jsonPtr, slug, req, this](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Article not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int articleId = r[0]["id"].as<int>();
                int authorId = r[0]["author_id"].as<int>();
                
                if (authorId != currentUserId) {
                    Json::Value error;
                    error["errors"]["body"].append("Forbidden");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k403Forbidden);
                    callback(resp);
                    return;
                }
                
                auto article = (*jsonPtr)["article"];
                
                std::vector<std::string> updates;
                std::vector<std::string> params;
                int paramCount = 1;
                std::string newSlug = slug;
                
                if (article.isMember("title")) {
                    std::string newTitle = article["title"].asString();
                    newSlug = generateSlug(newTitle);
                    updates.push_back("title = $" + std::to_string(paramCount++));
                    params.push_back(newTitle);
                    updates.push_back("slug = $" + std::to_string(paramCount++));
                    params.push_back(newSlug);
                }
                if (article.isMember("description")) {
                    updates.push_back("description = $" + std::to_string(paramCount++));
                    params.push_back(article["description"].asString());
                }
                if (article.isMember("body")) {
                    updates.push_back("body = $" + std::to_string(paramCount++));
                    params.push_back(article["body"].asString());
                }
                
                updates.push_back("updated_at = CURRENT_TIMESTAMP");
                
                if (updates.empty() || params.empty()) {
                    // Return current article
                    HttpRequestPtr newReq = HttpRequest::newHttpRequest();
                    newReq->setMethod(Get);
                    if (currentUserId > 0) {
                        newReq->addHeader("Authorization", req->getHeader("Authorization"));
                    }
                    // Just fetch and return the article
                    dbClient->execSqlAsync(
                        R"(SELECT a.id, a.slug, a.title, a.description, a.body,
                           a.created_at, a.updated_at,
                           u.username, u.bio, u.image,
                           CASE WHEN f1.follower_id IS NOT NULL THEN true ELSE false END as following,
                           CASE WHEN f2.user_id IS NOT NULL THEN true ELSE false END as favorited,
                           COALESCE(fav_count.count, 0) as favorites_count
                           FROM articles a
                           JOIN users u ON a.author_id = u.id
                           LEFT JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1
                           LEFT JOIN favorites f2 ON a.id = f2.article_id AND f2.user_id = $1
                           LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count 
                               ON a.id = fav_count.article_id
                           WHERE a.slug = $2)",
                        [callback, dbClient, articleId](const Result& r) {
                            if (r.empty()) {
                                Json::Value error;
                                error["errors"]["body"].append("Article not found");
                                auto resp = HttpResponse::newHttpJsonResponse(error);
                                resp->setStatusCode(k404NotFound);
                                callback(resp);
                                return;
                            }
                            
                            dbClient->execSqlAsync(
                                "SELECT tag FROM article_tags WHERE article_id = $1",
                                [callback, r](const Result& tagResult) {
                                    Json::Value article;
                                    article["slug"] = r[0]["slug"].as<std::string>();
                                    article["title"] = r[0]["title"].as<std::string>();
                                    article["description"] = r[0]["description"].as<std::string>();
                                    article["body"] = r[0]["body"].as<std::string>();
                                    article["createdAt"] = r[0]["created_at"].as<std::string>();
                                    article["updatedAt"] = r[0]["updated_at"].as<std::string>();
                                    article["favorited"] = r[0]["favorited"].as<bool>();
                                    article["favoritesCount"] = r[0]["favorites_count"].as<int>();
                                    
                                    Json::Value tagList(Json::arrayValue);
                                    for (size_t i = 0; i < tagResult.size(); i++) {
                                        tagList.append(tagResult[i]["tag"].as<std::string>());
                                    }
                                    article["tagList"] = tagList;
                                    
                                    Json::Value author;
                                    author["username"] = r[0]["username"].as<std::string>();
                                    author["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                                    author["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                                    author["following"] = r[0]["following"].as<bool>();
                                    article["author"] = author;
                                    
                                    Json::Value response;
                                    response["article"] = article;
                                    
                                    auto resp = HttpResponse::newHttpJsonResponse(response);
                                    callback(resp);
                                },
                                [callback](const DrogonDbException& e) {},
                                r[0]["id"].as<int>()
                            );
                        },
                        [callback](const DrogonDbException& e) {},
                        currentUserId, slug
                    );
                    return;
                }
                
                std::string sql = "UPDATE articles SET " + 
                                 std::accumulate(updates.begin(), updates.end(), std::string(),
                                     [](const std::string& a, const std::string& b) {
                                         return a.empty() ? b : a + ", " + b;
                                     }) + 
                                 " WHERE id = $" + std::to_string(paramCount) +
                                 " RETURNING updated_at";
                
                params.push_back(std::to_string(articleId));
                
                auto handleUpdateSuccess = [callback, dbClient, currentUserId, newSlug, articleId](const Result& r) {
                    // Fetch updated article
                    dbClient->execSqlAsync(
                        R"(SELECT a.id, a.slug, a.title, a.description, a.body,
                           a.created_at, a.updated_at,
                           u.username, u.bio, u.image,
                           CASE WHEN f1.follower_id IS NOT NULL THEN true ELSE false END as following,
                           CASE WHEN f2.user_id IS NOT NULL THEN true ELSE false END as favorited,
                           COALESCE(fav_count.count, 0) as favorites_count
                           FROM articles a
                           JOIN users u ON a.author_id = u.id
                           LEFT JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1
                           LEFT JOIN favorites f2 ON a.id = f2.article_id AND f2.user_id = $1
                           LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count 
                               ON a.id = fav_count.article_id
                           WHERE a.id = $2)",
                        [callback, dbClient](const Result& r) {
                            int articleId = r[0]["id"].as<int>();
                            
                            dbClient->execSqlAsync(
                                "SELECT tag FROM article_tags WHERE article_id = $1",
                                [callback, r](const Result& tagResult) {
                                    Json::Value article;
                                    article["slug"] = r[0]["slug"].as<std::string>();
                                    article["title"] = r[0]["title"].as<std::string>();
                                    article["description"] = r[0]["description"].as<std::string>();
                                    article["body"] = r[0]["body"].as<std::string>();
                                    article["createdAt"] = r[0]["created_at"].as<std::string>();
                                    article["updatedAt"] = r[0]["updated_at"].as<std::string>();
                                    article["favorited"] = r[0]["favorited"].as<bool>();
                                    article["favoritesCount"] = r[0]["favorites_count"].as<int>();
                                    
                                    Json::Value tagList(Json::arrayValue);
                                    for (size_t i = 0; i < tagResult.size(); i++) {
                                        tagList.append(tagResult[i]["tag"].as<std::string>());
                                    }
                                    article["tagList"] = tagList;
                                    
                                    Json::Value author;
                                    author["username"] = r[0]["username"].as<std::string>();
                                    author["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                                    author["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                                    author["following"] = r[0]["following"].as<bool>();
                                    article["author"] = author;
                                    
                                    Json::Value response;
                                    response["article"] = article;
                                    
                                    auto resp = HttpResponse::newHttpJsonResponse(response);
                                    callback(resp);
                                },
                                [callback](const DrogonDbException& e) {
                                    Json::Value error;
                                    error["errors"]["body"].append("Database error");
                                    auto resp = HttpResponse::newHttpJsonResponse(error);
                                    resp->setStatusCode(k422UnprocessableEntity);
                                    callback(resp);
                                },
                                r[0]["id"].as<int>()
                            );
                        },
                        [callback](const DrogonDbException& e) {
                            Json::Value error;
                            error["errors"]["body"].append("Database error");
                            auto resp = HttpResponse::newHttpJsonResponse(error);
                            resp->setStatusCode(k422UnprocessableEntity);
                            callback(resp);
                        },
                        currentUserId, articleId
                    );
                };
                
                auto handleUpdateError = [callback](const DrogonDbException& e) {
                    Json::Value error;
                    error["errors"]["body"].append("Database error");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k422UnprocessableEntity);
                    callback(resp);
                };
                
                // Call with appropriate number of parameters  
                if (params.size() == 1) {
                    dbClient->execSqlAsync(sql, handleUpdateSuccess, handleUpdateError, std::stoi(params[0]));
                } else if (params.size() == 2) {
                    dbClient->execSqlAsync(sql, handleUpdateSuccess, handleUpdateError, params[0], std::stoi(params[1]));
                } else if (params.size() == 3) {
                    dbClient->execSqlAsync(sql, handleUpdateSuccess, handleUpdateError, params[0], params[1], std::stoi(params[2]));
                } else if (params.size() == 4) {
                    dbClient->execSqlAsync(sql, handleUpdateSuccess, handleUpdateError, params[0], params[1], params[2], std::stoi(params[3]));
                }
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            slug
        );
    }

    void deleteArticle(const HttpRequestPtr& req,
                      std::function<void (const HttpResponsePtr &)> &&callback,
                      const std::string& slug)
    {
        int currentUserId;
        std::string currentUsername, currentEmail;
        
        if (!authenticateRequest(req, currentUserId, currentUsername, currentEmail)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "SELECT id, author_id FROM articles WHERE slug = $1",
            [callback, dbClient, currentUserId](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Article not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int authorId = r[0]["author_id"].as<int>();
                
                if (authorId != currentUserId) {
                    Json::Value error;
                    error["errors"]["body"].append("Forbidden");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k403Forbidden);
                    callback(resp);
                    return;
                }
                
                int articleId = r[0]["id"].as<int>();
                
                dbClient->execSqlAsync(
                    "DELETE FROM articles WHERE id = $1",
                    [callback](const Result& r) {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k200OK);
                        callback(resp);
                    },
                    [callback](const DrogonDbException& e) {
                        Json::Value error;
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    articleId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            slug
        );
    }

    void favoriteArticle(const HttpRequestPtr& req,
                        std::function<void (const HttpResponsePtr &)> &&callback,
                        const std::string& slug)
    {
        int currentUserId;
        std::string currentUsername, currentEmail;
        
        if (!authenticateRequest(req, currentUserId, currentUsername, currentEmail)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "SELECT id FROM articles WHERE slug = $1",
            [callback, dbClient, currentUserId, slug](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Article not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int articleId = r[0]["id"].as<int>();
                
                dbClient->execSqlAsync(
                    "INSERT INTO favorites (user_id, article_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
                    [callback, dbClient, currentUserId, slug](const Result& r) {
                        // Fetch the article
                        dbClient->execSqlAsync(
                            R"(SELECT a.id, a.slug, a.title, a.description, a.body,
                               a.created_at, a.updated_at,
                               u.username, u.bio, u.image,
                               CASE WHEN f1.follower_id IS NOT NULL THEN true ELSE false END as following,
                               true as favorited,
                               COALESCE(fav_count.count, 0) as favorites_count
                               FROM articles a
                               JOIN users u ON a.author_id = u.id
                               LEFT JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1
                               LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count 
                                   ON a.id = fav_count.article_id
                               WHERE a.slug = $2)",
                            [callback, dbClient](const Result& r) {
                                int articleId = r[0]["id"].as<int>();
                                
                                dbClient->execSqlAsync(
                                    "SELECT tag FROM article_tags WHERE article_id = $1",
                                    [callback, r](const Result& tagResult) {
                                        Json::Value article;
                                        article["slug"] = r[0]["slug"].as<std::string>();
                                        article["title"] = r[0]["title"].as<std::string>();
                                        article["description"] = r[0]["description"].as<std::string>();
                                        article["body"] = r[0]["body"].as<std::string>();
                                        article["createdAt"] = r[0]["created_at"].as<std::string>();
                                        article["updatedAt"] = r[0]["updated_at"].as<std::string>();
                                        article["favorited"] = true;
                                        article["favoritesCount"] = r[0]["favorites_count"].as<int>();
                                        
                                        Json::Value tagList(Json::arrayValue);
                                        for (size_t i = 0; i < tagResult.size(); i++) {
                                            tagList.append(tagResult[i]["tag"].as<std::string>());
                                        }
                                        article["tagList"] = tagList;
                                        
                                        Json::Value author;
                                        author["username"] = r[0]["username"].as<std::string>();
                                        author["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                                        author["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                                        author["following"] = r[0]["following"].as<bool>();
                                        article["author"] = author;
                                        
                                        Json::Value response;
                                        response["article"] = article;
                                        
                                        auto resp = HttpResponse::newHttpJsonResponse(response);
                                        callback(resp);
                                    },
                                    [callback](const DrogonDbException& e) {
                                        Json::Value error;
                                        error["errors"]["body"].append("Database error");
                                        auto resp = HttpResponse::newHttpJsonResponse(error);
                                        resp->setStatusCode(k422UnprocessableEntity);
                                        callback(resp);
                                    },
                                    r[0]["id"].as<int>()
                                );
                            },
                            [callback](const DrogonDbException& e) {
                                Json::Value error;
                                error["errors"]["body"].append("Database error");
                                auto resp = HttpResponse::newHttpJsonResponse(error);
                                resp->setStatusCode(k422UnprocessableEntity);
                                callback(resp);
                            },
                            currentUserId, slug
                        );
                    },
                    [callback](const DrogonDbException& e) {
                        Json::Value error;
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    currentUserId, articleId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            slug
        );
    }

    void unfavoriteArticle(const HttpRequestPtr& req,
                          std::function<void (const HttpResponsePtr &)> &&callback,
                          const std::string& slug)
    {
        int currentUserId;
        std::string currentUsername, currentEmail;
        
        if (!authenticateRequest(req, currentUserId, currentUsername, currentEmail)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "SELECT id FROM articles WHERE slug = $1",
            [callback, dbClient, currentUserId, slug](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Article not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int articleId = r[0]["id"].as<int>();
                
                dbClient->execSqlAsync(
                    "DELETE FROM favorites WHERE user_id = $1 AND article_id = $2",
                    [callback, dbClient, currentUserId, slug](const Result& r) {
                        // Fetch the article
                        dbClient->execSqlAsync(
                            R"(SELECT a.id, a.slug, a.title, a.description, a.body,
                               a.created_at, a.updated_at,
                               u.username, u.bio, u.image,
                               CASE WHEN f1.follower_id IS NOT NULL THEN true ELSE false END as following,
                               false as favorited,
                               COALESCE(fav_count.count, 0) as favorites_count
                               FROM articles a
                               JOIN users u ON a.author_id = u.id
                               LEFT JOIN followers f1 ON u.id = f1.followed_id AND f1.follower_id = $1
                               LEFT JOIN (SELECT article_id, COUNT(*) as count FROM favorites GROUP BY article_id) fav_count 
                                   ON a.id = fav_count.article_id
                               WHERE a.slug = $2)",
                            [callback, dbClient](const Result& r) {
                                int articleId = r[0]["id"].as<int>();
                                
                                dbClient->execSqlAsync(
                                    "SELECT tag FROM article_tags WHERE article_id = $1",
                                    [callback, r](const Result& tagResult) {
                                        Json::Value article;
                                        article["slug"] = r[0]["slug"].as<std::string>();
                                        article["title"] = r[0]["title"].as<std::string>();
                                        article["description"] = r[0]["description"].as<std::string>();
                                        article["body"] = r[0]["body"].as<std::string>();
                                        article["createdAt"] = r[0]["created_at"].as<std::string>();
                                        article["updatedAt"] = r[0]["updated_at"].as<std::string>();
                                        article["favorited"] = false;
                                        article["favoritesCount"] = r[0]["favorites_count"].as<int>();
                                        
                                        Json::Value tagList(Json::arrayValue);
                                        for (size_t i = 0; i < tagResult.size(); i++) {
                                            tagList.append(tagResult[i]["tag"].as<std::string>());
                                        }
                                        article["tagList"] = tagList;
                                        
                                        Json::Value author;
                                        author["username"] = r[0]["username"].as<std::string>();
                                        author["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                                        author["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                                        author["following"] = r[0]["following"].as<bool>();
                                        article["author"] = author;
                                        
                                        Json::Value response;
                                        response["article"] = article;
                                        
                                        auto resp = HttpResponse::newHttpJsonResponse(response);
                                        callback(resp);
                                    },
                                    [callback](const DrogonDbException& e) {
                                        Json::Value error;
                                        error["errors"]["body"].append("Database error");
                                        auto resp = HttpResponse::newHttpJsonResponse(error);
                                        resp->setStatusCode(k422UnprocessableEntity);
                                        callback(resp);
                                    },
                                    r[0]["id"].as<int>()
                                );
                            },
                            [callback](const DrogonDbException& e) {
                                Json::Value error;
                                error["errors"]["body"].append("Database error");
                                auto resp = HttpResponse::newHttpJsonResponse(error);
                                resp->setStatusCode(k422UnprocessableEntity);
                                callback(resp);
                            },
                            currentUserId, slug
                        );
                    },
                    [callback](const DrogonDbException& e) {
                        Json::Value error;
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    currentUserId, articleId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            slug
        );
    }

private:
    bool authenticateRequest(const HttpRequestPtr& req, int& userId, std::string& username, std::string& email)
    {
        auto authHeader = req->getHeader("Authorization");
        if (authHeader.empty()) {
            return false;
        }
        
        return realworld_utils::JWT::decode(authHeader, userId, username, email);
    }
    
    static std::string generateSlug(const std::string& title)
    {
        std::string slug = title;
        std::transform(slug.begin(), slug.end(), slug.begin(), ::tolower);
        
        for (size_t i = 0; i < slug.length(); i++) {
            if (!std::isalnum(slug[i])) {
                slug[i] = '-';
            }
        }
        
        // Remove consecutive dashes
        slug.erase(std::unique(slug.begin(), slug.end(), 
            [](char a, char b) { return a == '-' && b == '-'; }), slug.end());
        
        // Remove leading/trailing dashes
        if (!slug.empty() && slug[0] == '-') slug.erase(0, 1);
        if (!slug.empty() && slug[slug.length()-1] == '-') slug.erase(slug.length()-1);
        
        // Add random suffix
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 35);
        
        const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::string suffix;
        for (int i = 0; i < 6; i++) {
            suffix += chars[dis(gen)];
        }
        
        return slug + "-" + suffix;
    }
};
