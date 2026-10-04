#include "Database.h"
#include <drogon/drogon.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
using Params = std::vector<std::optional<std::string>>;

void send(const Callback& callback, int status, const Json::Value& body) {
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    callback(response);
}
void error(const Callback& callback, int status, const std::string& message) {
    Json::Value body; body["error"]["message"] = message; send(callback, status, body);
}
bool integer(const std::string& input, long long& number) {
    const auto [end, ec] = std::from_chars(input.data(), input.data()+input.size(), number);
    return !input.empty() && ec == std::errc{} && end == input.data()+input.size();
}
std::string readFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read " + path + "; start from the project folder");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Fixed SQL is assembled only from application-owned table/column names.
// All client-supplied values are passed separately through PQexecParams.
void query(Database& db, std::string sql, Params params, Callback callback,
           int success = 200, bool list = false) {
    const bool queued = db.submit([&db, sql = std::move(sql), params = std::move(params),
                                  callback, success, list](Database::Connection& connection) {
        try {
            if (!db.connect(connection)) { error(callback, 503, "Database unavailable"); return; }
            std::vector<const char*> values;
            for (const auto& value : params) values.push_back(value ? value->c_str() : nullptr);
            std::unique_ptr<PGresult, decltype(&PQclear)> result(
                PQexecParams(connection.get(), sql.c_str(), static_cast<int>(values.size()),
                             nullptr, values.data(), nullptr, nullptr, 0), PQclear);
            if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
                const char* state = result ? PQresultErrorField(result.get(), PG_DIAG_SQLSTATE) : nullptr;
                const std::string code = state ? state : "";
                if (code == "23505") error(callback, 409, "Email already exists");
                else if (code == "23503") error(callback, 409, "Record is referenced by another record");
                else if (code == "23514" || code == "23502" || code == "22003" || code == "22P02")
                    error(callback, 400, "Input violates a database constraint");
                else { LOG_ERROR << "Database query failed; SQLSTATE=" << code;
                       error(callback, 503, "Database operation failed"); }
                return;
            }
            const int count = PQntuples(result.get());
            if (!list && count == 0) { error(callback, 404, "Record not found"); return; }
            if (success == 204) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k204NoContent); callback(response); return;
            }
            Json::Value rows(Json::arrayValue);
            for (int row = 0; row < count; ++row) {
                Json::Value item;
                for (int column = 0; column < PQnfields(result.get()); ++column) {
                    const std::string name = PQfname(result.get(), column);
                    if (PQgetisnull(result.get(), row, column)) item[name] = Json::nullValue;
                    else {
                        const std::string value = PQgetvalue(result.get(), row, column);
                        if (name == "stock") item[name] = std::stoi(value);
                        else item[name] = value; // BIGINT IDs and exact decimal prices stay strings.
                    }
                }
                rows.append(item);
            }
            send(callback, success, list ? rows : rows[0]);
        } catch (const std::exception&) {
            error(callback, 500, "Internal server error");
        }
    });
    if (!queued) error(callback, 503, "Server busy; retry later");
}

bool validate(const Json::Value& body, bool users, Params& params, std::string& why) {
    if (!body.isObject()) { why = "Expected a JSON object"; return false; }
    const std::vector<std::string> allowed = users ? std::vector<std::string>{"name","email","phone"}
                                                 : std::vector<std::string>{"name","description","price","stock"};
    for (const auto& key : body.getMemberNames()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            why = "Unknown field: " + key; return false;
        }
    }
    auto text = [&](const char* key, size_t max, bool required) {
        if (!body.isMember(key) || body[key].isNull()) {
            if (required) { why = std::string(key) + " is required"; return false; }
            params.push_back(std::nullopt); return true;
        }
        if (!body[key].isString()) { why = std::string(key) + " must be a string"; return false; }
        const auto value = body[key].asString();
        if (value.size() > max || value.find('\0') != std::string::npos ||
            (required && value.find_first_not_of(" \t\r\n") == std::string::npos)) {
            why = std::string(key) + " is empty, too long, or invalid"; return false;
        }
        params.push_back(value); return true;
    };
    if (!text("name", 200, true)) return false;
    if (users) {
        if (!text("email", 254, true)) return false;
        static const std::regex email(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
        if (!std::regex_match(*params[1], email)) { why = "Invalid email format"; return false; }
        return text("phone", 50, false);
    }
    if (!text("description", 5000, false)) return false;
    static const std::regex money(R"(^[0-9]{1,10}(\.[0-9]{1,2})?$)");
    if (!body["price"].isString() || !std::regex_match(body["price"].asString(), money)) {
        why = "price must be a decimal string such as 199.99 (at most 10 integer and 2 fractional digits)";
        return false;
    }
    params.push_back(body["price"].asString());
    if (body.isMember("stock") && (!body["stock"].isInt() || body["stock"].asInt() < 0)) {
        why = "stock must be an integer from 0 to 2147483647"; return false;
    }
    params.push_back(std::to_string(body.get("stock", 0).asInt())); return true;
}

void registerCrud(Database& db, bool users) {
    const std::string table = users ? "users" : "products";
    const std::string base = "/api/v1/" + table;
    const std::string columns = users ? "id::text,name,email,phone,created_at,updated_at"
                                     : "id::text,name,description,price::text,stock,created_at";
    drogon::app().registerHandler(base,
        [&db, table, columns](const drogon::HttpRequestPtr& req, Callback&& cb) {
            const auto limitText = req->getParameter("limit");
            const auto offsetText = req->getParameter("offset");
            long long limit = 20, offset = 0;
            if ((!limitText.empty() && !integer(limitText, limit)) || limit < 1 || limit > 100 ||
                (!offsetText.empty() && !integer(offsetText, offset)) || offset < 0) {
                error(cb, 400, "limit must be 1..100 and offset must be non-negative"); return;
            }
            query(db, "SELECT " + columns + " FROM " + table + " ORDER BY id LIMIT $1 OFFSET $2",
                  {std::to_string(limit), std::to_string(offset)}, std::move(cb), 200, true);
        }, {drogon::Get});
    drogon::app().registerHandler(base,
        [&db, users, table, columns](const drogon::HttpRequestPtr& req, Callback&& cb) {
            const auto body = req->getJsonObject(); Params params; std::string why;
            if (!body || !validate(*body, users, params, why)) {
                error(cb, 400, body ? why : "Expected valid JSON"); return;
            }
            const auto fields = users ? "(name,email,phone) VALUES ($1,$2,$3)"
                                      : "(name,description,price,stock) VALUES ($1,$2,$3,$4)";
            query(db, "INSERT INTO " + table + fields + " RETURNING " + columns,
                  std::move(params), std::move(cb), 201);
        }, {drogon::Post});
    drogon::app().registerHandler(base + "/{1}",
        [&db, users, table, columns](const drogon::HttpRequestPtr& req, Callback&& cb, std::string id) {
            long long parsed = 0;
            if (!integer(id, parsed) || parsed <= 0) { error(cb, 400, "Invalid positive BIGINT id"); return; }
            if (req->method() == drogon::Get) {
                query(db, "SELECT " + columns + " FROM " + table + " WHERE id=$1", {id}, std::move(cb));
            } else if (req->method() == drogon::Delete) {
                query(db, "DELETE FROM " + table + " WHERE id=$1 RETURNING id", {id}, std::move(cb), 204);
            } else {
                const auto body = req->getJsonObject(); Params params; std::string why;
                if (!body || !validate(*body, users, params, why)) {
                    error(cb, 400, body ? why : "Expected valid JSON"); return;
                }
                params.push_back(id);
                const auto fields = users ? "name=$1,email=$2,phone=$3,updated_at=NOW() WHERE id=$4"
                                          : "name=$1,description=$2,price=$3,stock=$4 WHERE id=$5";
                query(db, "UPDATE " + table + " SET " + fields + " RETURNING " + columns,
                      std::move(params), std::move(cb));
            }
        }, {drogon::Get, drogon::Put, drogon::Delete});
}

int main() {
    try {
        const char* url = std::getenv("DATABASE_URL");
        if (!url || !*url) { std::cerr << "Set DATABASE_URL to your Neon connection string\n"; return 1; }
        const auto spec = readFile("public/openapi.json");
        const auto docs = readFile("public/docs.html");
        Database database(url);
        registerCrud(database, true); registerCrud(database, false);
        drogon::app().registerHandler("/openapi.json", [spec](const drogon::HttpRequestPtr&, Callback&& cb) {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setContentTypeCode(drogon::CT_APPLICATION_JSON); response->setBody(spec); cb(response);
        }, {drogon::Get});
        drogon::app().registerHandler("/docs", [docs](const drogon::HttpRequestPtr&, Callback&& cb) {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setContentTypeCode(drogon::CT_TEXT_HTML); response->setBody(docs); cb(response);
        }, {drogon::Get});
        drogon::app().registerHandler("/health", [](const drogon::HttpRequestPtr&, Callback&& cb) {
            Json::Value body; body["status"] = "ok"; send(cb, 200, body);
        }, {drogon::Get});
        drogon::app().registerHandler("/ready", [&database](const drogon::HttpRequestPtr&, Callback&& cb) {
            query(database, "SELECT 'ok'::text AS database", {}, std::move(cb));
        }, {drogon::Get});
        const char* listenHost = std::getenv("LISTEN_HOST");
        const char* uploadPath = std::getenv("UPLOAD_PATH");
        if (uploadPath && *uploadPath) drogon::app().setUploadPath(uploadPath);
        drogon::app().setClientMaxBodySize(64 * 1024)
            .addListener(listenHost && *listenHost ? listenHost : "127.0.0.1", 8080)
            .setThreadNum(2).run();
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
