#ifndef RAGREACTOR_API_ROUTER_H
#define RAGREACTOR_API_ROUTER_H

#include <string>
#include <memory>
#include <vector>

class SseStream;
class CommunityStore;

struct ApiRequest
{
    std::string method;
    std::string path;
    std::string content_type;
    std::string accept;
    std::string request_id;
    std::string query;
    std::string username;
    bool authenticated = false;
    bool csrf_valid = false;
    std::string body;
    CommunityStore *community_store = nullptr;
};

struct ApiResponse
{
    int status = 500;
    std::string reason = "Internal Server Error";
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::string> stream_chunks;
    std::shared_ptr<SseStream> stream_state;
    bool sse = false;
    bool close_connection = false;
    std::string request_id;
};

class ApiRouter
{
public:
    static const std::size_t MAX_API_BODY_SIZE = 16 * 1024;

    static ApiResponse route(const ApiRequest &request);

private:
    static ApiResponse health(const std::string &request_id);
    static ApiResponse metrics(const std::string &request_id);
    static ApiResponse ask(const ApiRequest &request);
    static ApiResponse community_feed(const ApiRequest &request);
    static ApiResponse community_action(const ApiRequest &request);
    static ApiResponse community_related(const ApiRequest &request);
    static ApiResponse error(int status, const std::string &code,
                             const std::string &message,
                             const std::string &request_id = "");
};

#endif
