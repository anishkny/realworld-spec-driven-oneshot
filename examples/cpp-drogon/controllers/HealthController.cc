#include <drogon/HttpSimpleController.h>

using namespace drogon;

class HealthController : public HttpSimpleController<HealthController>
{
public:
    void asyncHandleHttpRequest(const HttpRequestPtr& req,
                               std::function<void (const HttpResponsePtr &)> &&callback) override
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k200OK);
        callback(resp);
    }
    
    PATH_LIST_BEGIN
    PATH_ADD("/", Get);
    PATH_LIST_END
};
