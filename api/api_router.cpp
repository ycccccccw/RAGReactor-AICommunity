#include "api_router.h"
#include "../ai_rag/rag_service.h"
#include "sse_stream.h"
#include "metrics.h"

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
    ApiRequest routed = request;
    if (routed.request_id.empty()) routed.request_id = next_request_id();
    Metrics::instance().api_requests.fetch_add(1);

    if (routed.path == "/api/health")
    {
        if (routed.method != "GET")
            return error(405, "METHOD_NOT_ALLOWED", "only GET is allowed for this endpoint", routed.request_id);
        return health(routed.request_id);
    }

    if (routed.path == "/api/metrics")
    {
        if (routed.method != "GET")
            return error(405, "METHOD_NOT_ALLOWED", "only GET is allowed for this endpoint", routed.request_id);
        if (!routed.authenticated)
            return error(401, "AUTH_REQUIRED", "a valid login session is required", routed.request_id);
        return metrics(routed.request_id);
    }

    if (routed.path == "/api/ask")
    {
        Metrics::instance().ask_requests.fetch_add(1);
        if (routed.method != "POST")
            return error(405, "METHOD_NOT_ALLOWED", "only POST is allowed for this endpoint", routed.request_id);
        if (!routed.authenticated)
            return error(401, "AUTH_REQUIRED", "a valid login session is required", routed.request_id);
        if (!routed.csrf_valid)
            return error(403, "CSRF_REJECTED", "missing or invalid CSRF token", routed.request_id);
        return ask(routed);
    }

    return error(404, "API_NOT_FOUND", "the requested API endpoint does not exist", routed.request_id);
}

ApiResponse ApiRouter::health(const std::string &request_id)
{
    rag::RagService &service = rag::RagService::instance();
    json::object payload;
    payload["status"] = "ok";
    payload["service"] = "RAGReactor";
    payload["rag_configured"] = service.configured();
    payload["index_ready"] = service.index_ready();
    payload["provider"] = service.provider_name();
    payload["semantic_cache_entries"] = service.cache_size();
    payload["circuit_open"] = service.circuit_open();
    payload["retrieval_mode"] = service.retrieval_mode();
    payload["rerank_enabled"] = service.rerank_enabled();

    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = json::serialize(payload);
    response.request_id = request_id;
    return response;
}

ApiResponse ApiRouter::metrics(const std::string &request_id)
{
    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = Metrics::instance().json_snapshot();
    response.request_id = request_id;
    return response;
}

ApiResponse ApiRouter::ask(const ApiRequest &request)
{
    const std::string request_id = request.request_id;
    if (!contains_token(request.content_type, "application/json"))
        return error(415, "UNSUPPORTED_MEDIA_TYPE", "Content-Type must be application/json", request_id);

    if (request.body.size() > MAX_API_BODY_SIZE)
        return error(413, "PAYLOAD_TOO_LARGE", "API request body exceeds 16 KiB", request_id);

    json::error_code ec;
    json::value parsed = json::parse(request.body, ec);
    if (ec || !parsed.is_object())
        return error(400, "INVALID_JSON", "request body must be a valid JSON object", request_id);

    const json::object &object = parsed.as_object();
    const json::value *question_value = object.if_contains("question");
    if (!question_value || !question_value->is_string())
        return error(400, "INVALID_ARGUMENT", "question must be a string", request_id);

    std::string question(question_value->as_string());
    if (question.empty())
        return error(400, "INVALID_ARGUMENT", "question must not be empty", request_id);
    if (question.size() > 4096)
        return error(400, "INVALID_ARGUMENT", "question exceeds 4096 bytes", request_id);

    std::int64_t top_k = 5;
    if (const json::value *top_k_value = object.if_contains("top_k"))
    {
        if (!top_k_value->is_int64())
            return error(400, "INVALID_ARGUMENT", "top_k must be an integer", request_id);
        top_k = top_k_value->as_int64();
        if (top_k < 1 || top_k > 20)
            return error(400, "INVALID_ARGUMENT", "top_k must be between 1 and 20", request_id);
    }

    bool stream = contains_token(request.accept, "text/event-stream");
    if (const json::value *stream_value = object.if_contains("stream"))
    {
        if (!stream_value->is_bool())
            return error(400, "INVALID_ARGUMENT", "stream must be a boolean", request_id);
        stream = stream_value->as_bool();
    }

    if (stream)
    {
        ApiResponse response;
        response.status = 200;
        response.reason = "OK";
        response.content_type = "text/event-stream; charset=utf-8";
        response.sse = true;
        response.close_connection = true;
        response.request_id = request_id;
        response.stream_state = std::make_shared<SseStream>();
        // Send headers immediately. Retrieval and model I/O run outside the original
        // WebServer business pool, so login/upload/static requests remain responsive.
        response.stream_chunks.push_back(": connected\n\n");

        const std::shared_ptr<SseStream> state = response.stream_state;
        if (active_model_streams.fetch_add(1) >= MAX_ACTIVE_MODEL_STREAMS)
        {
            active_model_streams.fetch_sub(1);
            return error(503, "STREAM_LIMIT_REACHED",
                         "too many active model streams; retry later", request_id);
        }
        Metrics::instance().stream_active.fetch_add(1);
        std::thread([state, question, top_k, request_id]() {
            struct ActiveStreamGuard
            {
                ~ActiveStreamGuard() {
                    active_model_streams.fetch_sub(1);
                    Metrics::instance().stream_active.fetch_sub(1);
                }
            } active_stream_guard;
            const auto started = std::chrono::steady_clock::now();
            long long ttft_ms = -1;
            try
            {
                rag::PreparedRag prepared = rag::RagService::instance().prepare(
                    question, static_cast<std::size_t>(top_k));
                if (prepared.cache_hit) Metrics::instance().cache_hits.fetch_add(1);
                else Metrics::instance().cache_misses.fetch_add(1);
                if (prepared.rerank_fallback) Metrics::instance().rerank_failures.fetch_add(1);
                if (state->canceled())
                {
                    state->finish();
                    return;
                }

                json::object sources;
                sources["request_id"] = request_id;
                sources["documents"] = source_documents(prepared.sources);
                sources["cache_hit"] = prepared.cache_hit;
                sources["rerank_applied"] = prepared.rerank_applied;
                sources["rerank_fallback"] = prepared.rerank_fallback;
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
                    done["cache_hit"] = prepared.cache_hit;
                    done["ttft_ms"] = ttft_ms;
                    done["total_ms"] =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count();
                    state->push(sse_event("done", done));
                }
            }
            catch (const std::exception &exception)
            {
                Metrics::instance().upstream_failures.fetch_add(1);
                if (dynamic_cast<const rag::CircuitOpenError *>(&exception))
                    Metrics::instance().circuit_rejections.fetch_add(1);
                if (!state->canceled())
                {
                    json::object failure;
                    const bool circuit_open =
                        dynamic_cast<const rag::CircuitOpenError *>(&exception) != nullptr;
                    failure["code"] = circuit_open ? "RAG_CIRCUIT_OPEN" : "RAG_STREAM_ERROR";
                    failure["message"] = exception.what();
                    failure["request_id"] = request_id;
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
        Metrics::instance().upstream_failures.fetch_add(1);
        if (dynamic_cast<const rag::CircuitOpenError *>(&exception))
        {
            Metrics::instance().circuit_rejections.fetch_add(1);
            return error(503, "RAG_CIRCUIT_OPEN", exception.what(), request_id);
        }
        return error(502, "RAG_UPSTREAM_ERROR", exception.what(), request_id);
    }

    if (rag_answer.cache_hit) Metrics::instance().cache_hits.fetch_add(1);
    else Metrics::instance().cache_misses.fetch_add(1);
    if (rag_answer.rerank_fallback) Metrics::instance().rerank_failures.fetch_add(1);

    json::object payload;
    payload["request_id"] = request_id;
    payload["question"] = question;
    payload["top_k"] = top_k;
    payload["answer"] = rag_answer.text;
    payload["grounded"] = rag_answer.used_knowledge;
    payload["sources"] = source_documents(rag_answer.sources);
    payload["cache_hit"] = rag_answer.cache_hit;
    payload["rerank_applied"] = rag_answer.rerank_applied;
    payload["rerank_fallback"] = rag_answer.rerank_fallback;

    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = json::serialize(payload);
    response.request_id = request_id;
    return response;
}

ApiResponse ApiRouter::error(int status, const std::string &code,
                             const std::string &message, const std::string &request_id)
{
    json::object detail;
    detail["code"] = code;
    detail["message"] = message;

    json::object payload;
    payload["error"] = detail;
    if (!request_id.empty()) payload["request_id"] = request_id;

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
    response.request_id = request_id;
    return response;
}
