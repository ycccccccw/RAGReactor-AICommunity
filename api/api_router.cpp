#include "api_router.h"
#include "../ai_rag/rag_service.h"
#include "sse_stream.h"

#include <boost/json.hpp>
#include <atomic>
#include <sstream>
#include <thread>
#include <chrono>

namespace json = boost::json;

namespace
{
std::atomic<unsigned long long> request_sequence(0);
std::atomic<unsigned int> active_model_streams(0);
const unsigned int MAX_ACTIVE_MODEL_STREAMS = 16;

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

json::array source_documents(const std::vector<rag::SearchResult> &results)
{
    json::array documents;
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        json::object source;
        source["id"] = i + 1;
        source["file"] = results[i].chunk.source;
        source["chunk_index"] = results[i].chunk.chunk_index;
        source["score"] = results[i].score;
        source["text"] = results[i].chunk.text;
        documents.push_back(std::move(source));
    }
    return documents;
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
        if (!request.authenticated)
            return error(401, "AUTH_REQUIRED", "a valid login session is required");
        if (!request.csrf_valid)
            return error(403, "CSRF_REJECTED", "missing or invalid CSRF token");
        return ask(request);
    }

    return error(404, "API_NOT_FOUND", "the requested API endpoint does not exist");
}

ApiResponse ApiRouter::health()
{
    rag::RagService &service = rag::RagService::instance();
    json::object payload;
    payload["status"] = "ok";
    payload["service"] = "RAGReactor";
    payload["rag_configured"] = service.configured();
    payload["index_ready"] = service.index_ready();
    payload["provider"] = service.provider_name();

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
        response.stream_state = std::make_shared<SseStream>();
        // Send headers immediately. Retrieval and model I/O run outside the original
        // WebServer business pool, so login/upload/static requests remain responsive.
        response.stream_chunks.push_back(": connected\n\n");

        const std::shared_ptr<SseStream> state = response.stream_state;
        if (active_model_streams.fetch_add(1) >= MAX_ACTIVE_MODEL_STREAMS)
        {
            active_model_streams.fetch_sub(1);
            return error(503, "STREAM_LIMIT_REACHED",
                         "too many active model streams; retry later");
        }
        std::thread([state, question, top_k, request_id]() {
            struct ActiveStreamGuard
            {
                ~ActiveStreamGuard() { active_model_streams.fetch_sub(1); }
            } active_stream_guard;
            const auto started = std::chrono::steady_clock::now();
            long long ttft_ms = -1;
            try
            {
                rag::PreparedRag prepared = rag::RagService::instance().prepare(
                    question, static_cast<std::size_t>(top_k));
                if (state->canceled())
                {
                    state->finish();
                    return;
                }

                json::object sources;
                sources["request_id"] = request_id;
                sources["documents"] = source_documents(prepared.sources);
                if (!state->push(sse_event("sources", sources)))
                {
                    state->finish();
                    return;
                }

                bool emitted_delta = false;
                const auto emit_delta = [&](const std::string &text) {
                        emitted_delta = true;
                        if (ttft_ms < 0)
                            ttft_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started).count();
                        json::object delta;
                        delta["text"] = text;
                        return state->push(sse_event("delta", delta));
                    };
                try
                {
                    rag::RagService::instance().stream_answer(
                        prepared, emit_delta, state->cancellation_flag());
                }
                catch (...)
                {
                    // A retry is safe only before the first token. Retrying after a
                    // partial answer would duplicate content in the browser.
                    if (emitted_delta || state->canceled()) throw;
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    rag::RagService::instance().stream_answer(
                        prepared, emit_delta, state->cancellation_flag());
                }

                if (!state->canceled())
                {
                    json::object done;
                    done["finish_reason"] = "stop";
                    done["ttft_ms"] = ttft_ms;
                    done["total_ms"] =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count();
                    state->push(sse_event("done", done));
                }
            }
            catch (const std::exception &exception)
            {
                if (!state->canceled())
                {
                    json::object failure;
                    failure["code"] = "RAG_STREAM_ERROR";
                    failure["message"] = exception.what();
                    state->push(sse_event("error", failure));
                    json::object done;
                    done["finish_reason"] = "error";
                    state->push(sse_event("done", done));
                }
            }
            state->finish();
        }).detach();
        return response;
    }

    rag::RagAnswer rag_answer;
    try
    {
        rag_answer = rag::RagService::instance().ask(
            question, static_cast<std::size_t>(top_k));
    }
    catch (const std::exception &exception)
    {
        return error(502, "RAG_UPSTREAM_ERROR", exception.what());
    }

    json::object payload;
    payload["request_id"] = request_id;
    payload["question"] = question;
    payload["top_k"] = top_k;
    payload["answer"] = rag_answer.text;
    payload["grounded"] = rag_answer.used_knowledge;
    payload["sources"] = source_documents(rag_answer.sources);

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
    else if (status == 401) response.reason = "Unauthorized";
    else if (status == 403) response.reason = "Forbidden";
    else if (status == 404) response.reason = "Not Found";
    else if (status == 405) response.reason = "Method Not Allowed";
    else if (status == 413) response.reason = "Payload Too Large";
    else if (status == 415) response.reason = "Unsupported Media Type";
    else if (status == 502) response.reason = "Bad Gateway";
    else if (status == 503) response.reason = "Service Unavailable";
    else response.reason = "Internal Server Error";
    response.body = json::serialize(payload);
    return response;
}
