#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>
#include "../utils/Auth.h"

using namespace drogon;
using namespace orm;

class ProfileController : public HttpController<ProfileController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProfileController::getProfile, "/api/profiles/{username}", Get);
    ADD_METHOD_TO(ProfileController::followUser, "/api/profiles/{username}/follow", Post);
    ADD_METHOD_TO(ProfileController::unfollowUser, "/api/profiles/{username}/follow", Delete);
    METHOD_LIST_END

    void getProfile(const HttpRequestPtr& req,
                   std::function<void (const HttpResponsePtr &)> &&callback,
                   const std::string& username)
    {
        int currentUserId = 0;
        std::string currentUsername, currentEmail;
        authenticateRequest(req, currentUserId, currentUsername, currentEmail);

        auto dbClient = app().getDbClient("default");
        
        std::string sql = R"(
            SELECT u.username, u.bio, u.image,
                   CASE WHEN f.follower_id IS NOT NULL THEN true ELSE false END as following
            FROM users u
            LEFT JOIN followers f ON u.id = f.followed_id AND f.follower_id = $1
            WHERE u.username = $2
        )";
        
        dbClient->execSqlAsync(
            sql,
            [callback](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Profile not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                Json::Value response;
                response["profile"]["username"] = r[0]["username"].as<std::string>();
                response["profile"]["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                response["profile"]["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                response["profile"]["following"] = r[0]["following"].as<bool>();
                
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
            currentUserId, username
        );
    }

    void followUser(const HttpRequestPtr& req,
                   std::function<void (const HttpResponsePtr &)> &&callback,
                   const std::string& username)
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
        
        // First get the user to follow
        dbClient->execSqlAsync(
            "SELECT id, username, bio, image FROM users WHERE username = $1",
            [callback, dbClient, currentUserId](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("User not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int followedUserId = r[0]["id"].as<int>();
                std::string username = r[0]["username"].as<std::string>();
                std::string bio = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                auto image = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                
                // Insert follow relationship
                dbClient->execSqlAsync(
                    "INSERT INTO followers (follower_id, followed_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
                    [callback, username, bio, image](const Result& r) {
                        Json::Value response;
                        response["profile"]["username"] = username;
                        response["profile"]["bio"] = bio;
                        response["profile"]["image"] = image;
                        response["profile"]["following"] = true;
                        
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
                    currentUserId, followedUserId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            username
        );
    }

    void unfollowUser(const HttpRequestPtr& req,
                     std::function<void (const HttpResponsePtr &)> &&callback,
                     const std::string& username)
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
        
        // First get the user to unfollow
        dbClient->execSqlAsync(
            "SELECT id, username, bio, image FROM users WHERE username = $1",
            [callback, dbClient, currentUserId](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("User not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                int followedUserId = r[0]["id"].as<int>();
                std::string username = r[0]["username"].as<std::string>();
                std::string bio = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                auto image = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                
                // Delete follow relationship
                dbClient->execSqlAsync(
                    "DELETE FROM followers WHERE follower_id = $1 AND followed_id = $2",
                    [callback, username, bio, image](const Result& r) {
                        Json::Value response;
                        response["profile"]["username"] = username;
                        response["profile"]["bio"] = bio;
                        response["profile"]["image"] = image;
                        response["profile"]["following"] = false;
                        
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
                    currentUserId, followedUserId
                );
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            username
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
