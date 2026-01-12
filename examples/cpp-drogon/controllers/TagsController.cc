#include <drogon/HttpSimpleController.h>
#include <drogon/orm/DbClient.h>

using namespace drogon;
using namespace orm;

class TagsController : public HttpSimpleController<TagsController>
{
public:
    void asyncHandleHttpRequest(const HttpRequestPtr& req,
                               std::function<void (const HttpResponsePtr &)> &&callback) override
    {
        auto dbClient = app().getDbClient("default");
        
        dbClient->execSqlAsync(
            "SELECT DISTINCT tag FROM article_tags ORDER BY tag",
            [callback](const Result& r) {
                Json::Value tagsArray(Json::arrayValue);
                
                for (size_t i = 0; i < r.size(); i++) {
                    tagsArray.append(r[i]["tag"].as<std::string>());
                }
                
                Json::Value response;
                response["tags"] = tagsArray;
                
                auto resp = HttpResponse::newHttpJsonResponse(response);
                callback(resp);
            },
            [callback](const DrogonDbException& e) {
                Json::Value error;
                error["errors"]["body"].append("Database error");
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k422UnprocessableEntity);
                callback(resp);
            }
        );
    }
    
    PATH_LIST_BEGIN
    PATH_ADD("/api/tags", Get);
    PATH_LIST_END
};
