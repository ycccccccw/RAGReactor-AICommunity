#include "../api/api_router.h"
#include "../api/sse_stream.h"

#include <boost/json.hpp>
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

namespace json = boost::json;

int main()
{
    ApiRequest health_request;
    health_request.method = "GET";
    health_request.path = "/api/health";
    ApiResponse health = ApiRouter::route(health_request);
    assert(health.status == 200);
    assert(json::parse(health.body).as_object().at("status") == "ok");

    ApiRequest ask_request;
    ask_request.method = "POST";
    ask_request.path = "/api/ask";
    ask_request.content_type = "application/json";
    ask_request.authenticated = true;
    ask_request.csrf_valid = true;
    ask_request.body = R"({"question":"what is epoll?","top_k":3})";
    ApiResponse ask = ApiRouter::route(ask_request);
    // Unit tests deliberately run without a real API key and must fail safely.
    assert(ask.status == 502);
    assert(json::parse(ask.body).as_object().at("error").as_object().at("code") ==
           "RAG_UPSTREAM_ERROR");

    ask_request.body = "{bad json}";
    ApiResponse invalid_json = ApiRouter::route(ask_request);
    assert(invalid_json.status == 400);

    ask_request.body = R"({"question":"stream","stream":true})";
    ApiResponse stream = ApiRouter::route(ask_request);
    assert(stream.status == 200);
    assert(stream.sse);
    assert(stream.stream_state);
    bool received_error = false;
    for (int attempt = 0; attempt < 100 && !stream.stream_state->finished_and_empty(); ++attempt)
    {
        std::string event;
        while (stream.stream_state->try_pop(event))
            if (event.find("event: error") != std::string::npos) received_error = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::string event;
    while (stream.stream_state->try_pop(event))
        if (event.find("event: error") != std::string::npos) received_error = true;
    assert(received_error);

    ApiRequest missing;
    missing.method = "GET";
    missing.path = "/api/missing";
    assert(ApiRouter::route(missing).status == 404);

    ApiRequest wrong_method;
    wrong_method.method = "GET";
    wrong_method.path = "/api/ask";
    assert(ApiRouter::route(wrong_method).status == 405);

    ApiRequest unauthorized;
    unauthorized.method = "POST";
    unauthorized.path = "/api/ask";
    unauthorized.content_type = "application/json";
    unauthorized.body = R"({"question":"secret"})";
    assert(ApiRouter::route(unauthorized).status == 401);
    unauthorized.authenticated = true;
    assert(ApiRouter::route(unauthorized).status == 403);

    std::cout << "api_router_test: all checks passed\n";
    return 0;
}
