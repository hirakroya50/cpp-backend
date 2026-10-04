#include <drogon/drogon.h>

#include <functional>

using ResponseCallback =
    std::function<void(const drogon::HttpResponsePtr&)>;

int main()
{
    // GET /health
    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr&,
           ResponseCallback&& callback)
        {
            Json::Value body;
            body["status"] = "updateddd----";

            callback(
                drogon::HttpResponse::newHttpJsonResponse(body)
            );
        },
        {drogon::Get}
    );

    // POST /api/v1/todos/validate
    drogon::app().registerHandler(
        "/api/v1/todos/validate",
        [](const drogon::HttpRequestPtr& request,
           ResponseCallback&& callback)
        {
            const auto json = request->getJsonObject();

            if (!json ||
                !json->isObject() ||
                !(*json)["title"].isString())
            {
                Json::Value body;
                body["error"]["code"] = "INVALID_INPUT";
                body["error"]["message"] =
                    "title must be a string";

                auto response =
                    drogon::HttpResponse::newHttpJsonResponse(body);

                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }

            const auto title = (*json)["title"].asString();

            if (title.empty() ||
                title.size() > 200 ||
                title.find_first_not_of(" \t\r\n") ==
                    std::string::npos)
            {
                Json::Value body;
                body["error"]["code"] = "INVALID_TITLE";
                body["error"]["message"] =
                    "title must contain text and be at most 200 bytes";

                auto response =
                    drogon::HttpResponse::newHttpJsonResponse(body);

                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }

            Json::Value body;
            body["valid"] = true;
            body["title"] = title;

            callback(
                drogon::HttpResponse::newHttpJsonResponse(body)
            );
        },
        {drogon::Post}
    );

    drogon::app()
        .addListener("127.0.0.1", 8080)
        .setThreadNum(2)
        .run();
}