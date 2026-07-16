#ifndef RAGREACTOR_API_ROUTER_H
#define RAGREACTOR_API_ROUTER_H

#include <string>
#include <vector>

struct ApiRequest
{
    std::string method;
    std::string path;
    std::string content_type;
    std::string accept;
    std::string body;
};

struct ApiResponse
{
    int status = 500;
    std::string reason = "Internal Server Error";
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::string> stream_chunks;
    bool sse = false;
    bool close_connection = false;
};

class ApiRouter
{
public:
    static const std::size_t MAX_API_BODY_SIZE = 16 * 1024;

    static ApiResponse route(const ApiRequest &request);

private:
    static ApiResponse health();
    static ApiResponse ask(const ApiRequest &request);
    static ApiResponse error(int status, const std::string &code,
                             const std::string &message);
};

#endif
