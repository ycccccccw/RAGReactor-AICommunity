#include "api_router.h"

#include <boost/json.hpp>
#include <atomic>
#include <sstream>

namespace json = boost::json;

namespace
{
std::atomic<unsigned long long> request_sequence(0);

std::string next_request_id()
{
    std::ostringstream out;
    out << "req-" << ++request_sequence;
    return out.str();
}

bool contains_token(const std::string &value, const std::string &token)
{
    return value.find(token) != std::string::npos;
}

std::string sse_event(const std::string &event, const json::value &data)
{
    return "event: " + event + "\n" + "data: " + json::serialize(data) + "\n\n";
}
}

ApiResponse ApiRouter::route(const ApiRequest &request)
{
    if (request.path == "/api/health")
    {
        if (request.method != "GET")
            return error(405, "METHOD_NOT_ALLOWED", "only GET is allowed for this endpoint");
        return health();
    }

    if (request.path == "/api/ask")
    {
        if (request.method != "POST")
            return error(405, "METHOD_NOT_ALLOWED", "only POST is allowed for this endpoint");
        return ask(request);
    }

    return error(404, "API_NOT_FOUND", "the requested API endpoint does not exist");
}

ApiResponse ApiRouter::health()
{
    json::object payload;
    payload["status"] = "ok";
    payload["service"] = "RAGReactor";
    payload["rag_ready"] = false;
    payload["provider"] = "mock";

    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = json::serialize(payload);
    return response;
}

ApiResponse ApiRouter::ask(const ApiRequest &request)
{
    if (!contains_token(request.content_type, "application/json"))
        return error(415, "UNSUPPORTED_MEDIA_TYPE", "Content-Type must be application/json");

    if (request.body.size() > MAX_API_BODY_SIZE)
        return error(413, "PAYLOAD_TOO_LARGE", "API request body exceeds 16 KiB");

    json::error_code ec;
    json::value parsed = json::parse(request.body, ec);
    if (ec || !parsed.is_object())
        return error(400, "INVALID_JSON", "request body must be a valid JSON object");

    const json::object &object = parsed.as_object();
    const json::value *question_value = object.if_contains("question");
    if (!question_value || !question_value->is_string())
        return error(400, "INVALID_ARGUMENT", "question must be a string");

    std::string question(question_value->as_string());
    if (question.empty())
        return error(400, "INVALID_ARGUMENT", "question must not be empty");
    if (question.size() > 4096)
        return error(400, "INVALID_ARGUMENT", "question exceeds 4096 bytes");

    std::int64_t top_k = 5;
    if (const json::value *top_k_value = object.if_contains("top_k"))
    {
        if (!top_k_value->is_int64())
            return error(400, "INVALID_ARGUMENT", "top_k must be an integer");
        top_k = top_k_value->as_int64();
        if (top_k < 1 || top_k > 20)
            return error(400, "INVALID_ARGUMENT", "top_k must be between 1 and 20");
    }

    bool stream = contains_token(request.accept, "text/event-stream");
    if (const json::value *stream_value = object.if_contains("stream"))
    {
        if (!stream_value->is_bool())
            return error(400, "INVALID_ARGUMENT", "stream must be a boolean");
        stream = stream_value->as_bool();
    }

    const std::string request_id = next_request_id();
    if (stream)
    {
        ApiResponse response;
        response.status = 200;
        response.reason = "OK";
        response.content_type = "text/event-stream; charset=utf-8";
        response.sse = true;
        response.close_connection = true;

        json::object sources;
        sources["request_id"] = request_id;
        sources["documents"] = json::array();
        response.stream_chunks.push_back(sse_event("sources", sources));

        json::object first_delta;
        first_delta["text"] = "这是 RAGReactor 的 ";
        response.stream_chunks.push_back(sse_event("delta", first_delta));

        json::object second_delta;
        second_delta["text"] = "Mock 流式回答。真实知识库将在下一阶段接入。";
        response.stream_chunks.push_back(sse_event("delta", second_delta));

        json::object done;
        done["finish_reason"] = "stop";
        response.stream_chunks.push_back(sse_event("done", done));
        return response;
    }

    json::object payload;
    payload["request_id"] = request_id;
    payload["question"] = question;
    payload["top_k"] = top_k;
    payload["answer"] = "当前为 Mock 回答，RAG 服务将在后续阶段接入。";
    payload["sources"] = json::array();

    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = json::serialize(payload);
    return response;
}

ApiResponse ApiRouter::error(int status, const std::string &code,
                             const std::string &message)
{
    json::object detail;
    detail["code"] = code;
    detail["message"] = message;

    json::object payload;
    payload["error"] = detail;

    ApiResponse response;
    response.status = status;
    if (status == 400) response.reason = "Bad Request";
    else if (status == 404) response.reason = "Not Found";
    else if (status == 405) response.reason = "Method Not Allowed";
    else if (status == 413) response.reason = "Payload Too Large";
    else if (status == 415) response.reason = "Unsupported Media Type";
    else response.reason = "Internal Server Error";
    response.body = json::serialize(payload);
    return response;
}
