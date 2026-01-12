#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>
#include "../utils/Auth.h"
#include <numeric>

using namespace drogon;
using namespace orm;

class UserController : public HttpController<UserController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::registerUser, "/api/users", Post);
    ADD_METHOD_TO(UserController::login, "/api/users/login", Post);
    ADD_METHOD_TO(UserController::getCurrentUser, "/api/user", Get);
    ADD_METHOD_TO(UserController::updateUser, "/api/user", Put);
    METHOD_LIST_END

    void registerUser(const HttpRequestPtr& req,
                     std::function<void (const HttpResponsePtr &)> &&callback)
    {
        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr || !(*jsonPtr).isMember("user")) {
            Json::Value error;
            error["errors"]["body"].append("Invalid request format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto user = (*jsonPtr)["user"];
        
        if (!user.isMember("username") || !user.isMember("email") || !user.isMember("password")) {
            Json::Value error;
            error["errors"]["body"].append("Missing required fields");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        std::string username = user["username"].asString();
        std::string email = user["email"].asString();
        std::string password = user["password"].asString();
        
        // Basic email validation
        if (email.find('@') == std::string::npos) {
            Json::Value error;
            error["errors"]["body"].append("Invalid email format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto dbClient = app().getDbClient("default");
        std::string hashedPassword = realworld_utils::Password::hash(password);
        
        dbClient->execSqlAsync(
            "INSERT INTO users (username, email, password) VALUES ($1, $2, $3) RETURNING id",
            [callback, username, email](const Result& r) {
                int userId = r[0]["id"].as<int>();
                std::string token = realworld_utils::JWT::encode(userId, username, email);
                
                Json::Value response;
                response["user"]["email"] = email;
                response["user"]["token"] = token;
                response["user"]["username"] = username;
                response["user"]["bio"] = "";
                response["user"]["image"] = Json::Value::null;
                
                auto resp = HttpResponse::newHttpJsonResponse(response);
                callback(resp);
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Username or email already exists");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            },
            username, email, hashedPassword
        );
    }

    void login(const HttpRequestPtr& req,
              std::function<void (const HttpResponsePtr &)> &&callback)
    {
        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr || !(*jsonPtr).isMember("user")) {
            Json::Value error;
            error["errors"]["body"].append("Invalid request format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto user = (*jsonPtr)["user"];
        
        if (!user.isMember("email") || !user.isMember("password")) {
            Json::Value error;
            error["errors"]["body"].append("Missing required fields");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        std::string email = user["email"].asString();
        std::string password = user["password"].asString();
        std::string hashedPassword = realworld_utils::Password::hash(password);

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "SELECT id, username, email, bio, image, password FROM users WHERE email = $1",
            [callback, hashedPassword](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("Invalid email or password");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k401Unauthorized);
                    callback(resp);
                    return;
                }
                
                std::string storedPassword = r[0]["password"].as<std::string>();
                if (storedPassword != hashedPassword) {
                    Json::Value error;
                    error["errors"]["body"].append("Invalid email or password");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k401Unauthorized);
                    callback(resp);
                    return;
                }
                
                int userId = r[0]["id"].as<int>();
                std::string username = r[0]["username"].as<std::string>();
                std::string email = r[0]["email"].as<std::string>();
                std::string token = realworld_utils::JWT::encode(userId, username, email);
                
                Json::Value response;
                response["user"]["email"] = email;
                response["user"]["token"] = token;
                response["user"]["username"] = username;
                response["user"]["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                response["user"]["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                
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
            email
        );
    }

    void getCurrentUser(const HttpRequestPtr& req,
                       std::function<void (const HttpResponsePtr &)> &&callback)
    {
        int userId;
        std::string username, email;
        
        if (!authenticateRequest(req, userId, username, email)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "SELECT username, email, bio, image FROM users WHERE id = $1",
            [callback, userId, username, email](const Result& r) {
                if (r.empty()) {
                    Json::Value error;
                    error["errors"]["body"].append("User not found");
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k404NotFound);
                    callback(resp);
                    return;
                }
                
                std::string token = realworld_utils::JWT::encode(userId, username, email);
                
                Json::Value response;
                response["user"]["email"] = r[0]["email"].as<std::string>();
                response["user"]["token"] = token;
                response["user"]["username"] = r[0]["username"].as<std::string>();
                response["user"]["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
                response["user"]["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
                
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
            userId
        );
    }

    void updateUser(const HttpRequestPtr& req,
                   std::function<void (const HttpResponsePtr &)> &&callback)
    {
        int userId;
        std::string username, email;
        
        if (!authenticateRequest(req, userId, username, email)) {
            Json::Value error;
            error["errors"]["body"].append("Unauthorized");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }

        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr || !(*jsonPtr).isMember("user")) {
            Json::Value error;
            error["errors"]["body"].append("Invalid request format");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
            return;
        }

        auto user = (*jsonPtr)["user"];
        
        // Validate email format if provided
        if (user.isMember("email")) {
            std::string email = user["email"].asString();
            if (email.find('@') == std::string::npos) {
                Json::Value error;
                error["errors"]["body"].append("Invalid email format");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
                return;
            }
        }
        
        std::vector<std::string> updates;
        std::vector<std::string> params;
        int paramCount = 1;
        
        if (user.isMember("email")) {
            updates.push_back("email = $" + std::to_string(paramCount++));
            params.push_back(user["email"].asString());
        }
        if (user.isMember("username")) {
            updates.push_back("username = $" + std::to_string(paramCount++));
            params.push_back(user["username"].asString());
        }
        if (user.isMember("password")) {
            updates.push_back("password = $" + std::to_string(paramCount++));
            params.push_back(realworld_utils::Password::hash(user["password"].asString()));
        }
        if (user.isMember("bio")) {
            updates.push_back("bio = $" + std::to_string(paramCount++));
            params.push_back(user["bio"].asString());
        }
        if (user.isMember("image")) {
            updates.push_back("image = $" + std::to_string(paramCount++));
            params.push_back(user["image"].asString());
        }
        
        if (updates.empty()) {
            getCurrentUser(req, std::move(callback));
            return;
        }

        std::string sql = "UPDATE users SET " + 
                         std::accumulate(updates.begin(), updates.end(), std::string(),
                             [](const std::string& a, const std::string& b) {
                                 return a.empty() ? b : a + ", " + b;
                             }) + 
                         " WHERE id = $" + std::to_string(paramCount) +
                         " RETURNING username, email, bio, image";
        
        params.push_back(std::to_string(userId));

        auto dbClient = app().getDbClient("default");
        
        auto handleSuccess = [callback, userId](const Result& r) {
            if (r.empty()) {
                Json::Value error;
                error["errors"]["body"].append("User not found");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k404NotFound);
                callback(resp);
                return;
            }
            
            std::string username = r[0]["username"].as<std::string>();
            std::string email = r[0]["email"].as<std::string>();
            std::string token = realworld_utils::JWT::encode(userId, username, email);
            
            Json::Value response;
            response["user"]["email"] = email;
            response["user"]["token"] = token;
            response["user"]["username"] = username;
            response["user"]["bio"] = r[0]["bio"].isNull() ? "" : r[0]["bio"].as<std::string>();
            response["user"]["image"] = r[0]["image"].isNull() ? Json::Value::null : r[0]["image"].as<std::string>();
            
            auto resp = HttpResponse::newHttpJsonResponse(response);
            callback(resp);
        };
        
        auto handleError = [callback](const DrogonDbException& e) {
            Json::Value error;
            error["errors"]["body"].append("Database error");
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k422UnprocessableEntity);
            callback(resp);
        };
        
        // Call with appropriate number of parameters
        if (params.size() == 1) {
            dbClient->execSqlAsync(sql, handleSuccess, handleError, std::stoi(params[0]));
        } else if (params.size() == 2) {
            dbClient->execSqlAsync(sql, handleSuccess, handleError, params[0], std::stoi(params[1]));
        } else if (params.size() == 3) {
            dbClient->execSqlAsync(sql, handleSuccess, handleError, params[0], params[1], std::stoi(params[2]));
        } else if (params.size() == 4) {
            dbClient->execSqlAsync(sql, handleSuccess, handleError, params[0], params[1], params[2], std::stoi(params[3]));
        } else if (params.size() == 5) {
            dbClient->execSqlAsync(sql, handleSuccess, handleError, params[0], params[1], params[2], params[3], std::stoi(params[4]));
        } else if (params.size() == 6) {
            dbClient->execSqlAsync(sql, handleSuccess, handleError, params[0], params[1], params[2], params[3], params[4], std::stoi(params[5]));
        }
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
