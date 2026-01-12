#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>
#include "../utils/Auth.h"

using namespace drogon;
using namespace orm;

class CommentController : public HttpController<CommentController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CommentController::getComments, "/api/articles/{slug}/comments", Get);
    ADD_METHOD_TO(CommentController::addComment, "/api/articles/{slug}/comments", Post);
    ADD_METHOD_TO(CommentController::deleteComment, "/api/articles/{slug}/comments/{id}", Delete);
    METHOD_LIST_END

    void getComments(const HttpRequestPtr& req,
                    std::function<void (const HttpResponsePtr &)> &&callback,
                    const std::string& slug)
    {
        int currentUserId = 0;
        std::string currentUsername, currentEmail;
        authenticateRequest(req, currentUserId, currentUsername, currentEmail);

        auto dbClient = app().getDbClient("default");
        
        // First check if article exists
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
                
                std::string sql = R"(
                    SELECT c.id, c.body, c.created_at, c.updated_at,
                           u.username, u.bio, u.image,
                           CASE WHEN f.follower_id IS NOT NULL THEN true ELSE false END as following
                    FROM comments c
                    JOIN articles a ON c.article_id = a.id
                    JOIN users u ON c.author_id = u.id
                    LEFT JOIN followers f ON u.id = f.followed_id AND f.follower_id = $1
                    WHERE a.slug = $2
                    ORDER BY c.created_at ASC
                )";
                
                dbClient->execSqlAsync(
                    sql,
                    [callback](const Result& r) {
                        Json::Value commentsArray(Json::arrayValue);
                        
                        for (size_t i = 0; i < r.size(); i++) {
                            Json::Value comment;
                            comment["id"] = r[i]["id"].as<int>();
                            comment["body"] = r[i]["body"].as<std::string>();
                            comment["createdAt"] = r[i]["created_at"].as<std::string>();
                            comment["updatedAt"] = r[i]["updated_at"].as<std::string>();
                            
                            Json::Value author;
                            author["username"] = r[i]["username"].as<std::string>();
                            author["bio"] = r[i]["bio"].isNull() ? "" : r[i]["bio"].as<std::string>();
                            author["image"] = r[i]["image"].isNull() ? Json::Value::null : r[i]["image"].as<std::string>();
                            author["following"] = r[i]["following"].as<bool>();
                            comment["author"] = author;
                            
                            commentsArray.append(comment);
                        }
                        
                        Json::Value response;
                        response["comments"] = commentsArray;
                        
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
            slug
        );
    }

    void addComment(const HttpRequestPtr& req,
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
        if (!jsonPtr || !(*jsonPtr).isMember("comment")) {
            Json::Value error;
            error["errors"]["body"].append("Invalid request format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto comment = (*jsonPtr)["comment"];
        
        if (!comment.isMember("body")) {
            Json::Value error;
            error["errors"]["body"].append("Missing required field: body");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        std::string body = comment["body"].asString();

        auto dbClient = app().getDbClient("default");
        
        // First get article id
        dbClient->execSqlAsync(
            "SELECT id FROM articles WHERE slug = $1",
            [callback, dbClient, currentUserId, body](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Article not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int articleId = r[0]["id"].as<int>();
                
                // Insert comment
                dbClient->execSqlAsync(
                    "INSERT INTO comments (body, article_id, author_id) VALUES ($1, $2, $3) RETURNING id, created_at, updated_at",
                    [callback, dbClient, currentUserId, body](const Result& r) {
                        int commentId = r[0]["id"].as<int>();
                        std::string createdAt = r[0]["created_at"].as<std::string>();
                        std::string updatedAt = r[0]["updated_at"].as<std::string>();
                        
                        // Get user info
                        dbClient->execSqlAsync(
                            "SELECT username, bio, image FROM users WHERE id = $1",
                            [callback, commentId, body, createdAt, updatedAt](const Result& r) {
                                Json::Value response;
                                Json::Value comment;
                                
                                comment["id"] = commentId;
                                comment["body"] = body;
                                comment["createdAt"] = createdAt;
                                comment["updatedAt"] = updatedAt;
                                
                                Json::Value author;
                                author["username"] = r[0]["username"].as<std::string>();
                                author["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                                author["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                                author["following"] = false;
                                comment["author"] = author;
                                
                                response["comment"] = comment;
                                
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
                        error["errors"]["body"].append("Database error");
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k422UnprocessableEntity);
                        callback(resp);
                    },
                    body, articleId, currentUserId
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

    void deleteComment(const HttpRequestPtr& req,
                      std::function<void (const HttpResponsePtr &)> &&callback,
                      const std::string& slug,
                      int commentId)
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
        
        // Check if comment exists and user owns it
        dbClient->execSqlAsync(
            "SELECT c.author_id FROM comments c JOIN articles a ON c.article_id = a.id WHERE c.id = $1 AND a.slug = $2",
            [callback, dbClient, currentUserId, commentId](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Comment not found");
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
                
                // Delete comment
                dbClient->execSqlAsync(
                    "DELETE FROM comments WHERE id = $1",
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
                    commentId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            commentId, slug
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
};
