#include "http_json_client.h"

#include <boost/json.hpp>
#include <curl/curl.h>

#include <mutex>
#include <stdexcept>

namespace json = boost::json;

namespace rag
{
namespace
{
std::once_flag curl_init_once;

void initialize_curl()
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("failed to initialize libcurl");
}

std::size_t receive_body(char *data, std::size_t size, std::size_t count,
                         void *userdata)
{
    const std::size_t bytes = size * count;
    static_cast<std::string *>(userdata)->append(data, bytes);
    return bytes;
}

std::string api_error_message(const std::string &response)
{
    json::error_code ec;
    json::value parsed = json::parse(response, ec);
    if (ec || !parsed.is_object()) return response.substr(0, 500);
    const json::object &object = parsed.as_object();
    const json::value *error = object.if_contains("error");
    if (!error) return response.substr(0, 500);
    if (error->is_string()) return std::string(error->as_string());
    if (error->is_object())
    {
        const json::value *message = error->as_object().if_contains("message");
        if (message && message->is_string()) return std::string(message->as_string());
    }
    return json::serialize(*error).substr(0, 500);
}
}

HttpJsonClient::HttpJsonClient(long connect_timeout_ms, long request_timeout_ms)
    : connect_timeout_ms_(connect_timeout_ms), request_timeout_ms_(request_timeout_ms)
{
    std::call_once(curl_init_once, initialize_curl);
}

json::value HttpJsonClient::post(const std::string &url, const std::string &api_key,
                                 const json::value &body) const
{
    CURL *curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to create HTTP client");

    const std::string request = json::serialize(body);
    const std::string authorization = "Authorization: Bearer " + api_key;
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, authorization.c_str());

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RAGReactor/1.0");

    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK)
        throw std::runtime_error(std::string("model HTTP request failed: ") +
                                 curl_easy_strerror(result));
    if (status < 200 || status >= 300)
        throw std::runtime_error("model API returned HTTP " + std::to_string(status) +
                                 ": " + api_error_message(response));

    json::error_code ec;
    json::value parsed = json::parse(response, ec);
    if (ec) throw std::runtime_error("model API returned invalid JSON");
    return parsed;
}
}
