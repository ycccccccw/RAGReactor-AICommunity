#include "api_router.h"
#include "../ai_rag/rag_service.h"
#include "sse_stream.h"
#include "metrics.h"
#include "community_store.h"

#include <boost/json.hpp>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
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

std::string url_decode(const std::string &value, bool &valid)
{
    valid = true;
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '+') decoded.push_back(' ');
        else if (value[i] == '%')
        {
            if (i + 2 >= value.size() || !std::isxdigit(static_cast<unsigned char>(value[i + 1])) ||
                !std::isxdigit(static_cast<unsigned char>(value[i + 2])))
            {
                valid = false;
                return "";
            }
            decoded.push_back(static_cast<char>(std::strtoul(value.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        }
        else decoded.push_back(value[i]);
    }
    return decoded;
}

bool parse_query(const std::string &query, std::map<std::string, std::string> &values)
{
    values.clear();
    std::size_t start = 0;
    while (start <= query.size())
    {
        const std::size_t end = query.find('&', start);
        const std::string pair = query.substr(start, end == std::string::npos ? end : end - start);
        if (!pair.empty())
        {
            const std::size_t equals = pair.find('=');
            bool key_valid = false, value_valid = false;
            const std::string key = url_decode(pair.substr(0, equals), key_valid);
            const std::string value = url_decode(equals == std::string::npos ? "" : pair.substr(equals + 1), value_valid);
            if (!key_valid || !value_valid || key.empty() || values.count(key)) return false;
            values[key] = value;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool parse_unsigned_decimal(const std::string &value, unsigned long long &number)
{
    if (value.empty()) return false;
    unsigned long long parsed = 0;
    for (char ch : value)
    {
        if (ch < '0' || ch > '9') return false;
        const unsigned int digit = static_cast<unsigned int>(ch - '0');
        if (parsed > (std::numeric_limits<unsigned long long>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    number = parsed;
    return true;
}

bool valid_event_id(const std::string &value)
{
    if (value.size() < 16 || value.size() > 36) return false;
    for (char ch : value)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') return false;
    return true;
}

bool valid_action_type(const std::string &value)
{
    static const char *types[] = {"impression", "open", "dwell", "like", "unlike",
                                  "collect", "uncollect", "skip", "dislike"};
    for (const char *type : types) if (value == type) return true;
    return false;
}

std::string rfc3339_time(std::string value)
{
    if (value.size() == 19 && value[10] == ' ')
    {
        value[10] = 'T';
        value += 'Z';
    }
    return value;
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
        source["source_type"] = results[i].chunk.source_type;
        source["source_id"] = results[i].chunk.source_id;
        source["author"] = results[i].chunk.author;
        source["created_at"] = results[i].chunk.created_at;
        source["status"] = results[i].chunk.status;
        source["trust_level"] = results[i].chunk.trust_level;
        documents.push_back(std::move(source));
    }
    return documents;
}

json::array community_source_documents(const std::vector<rag::SearchResult> &results)
{
    std::vector<rag::SearchResult> community;
    for (const auto &result : results)
        if (result.chunk.source_type == "community") community.push_back(result);
    return source_documents(community);
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

    if (routed.path == "/api/community/feed")
    {
        if (routed.method != "GET")
            return error(405, "METHOD_NOT_ALLOWED", "only GET is allowed for this endpoint", routed.request_id);
        if (!routed.authenticated)
            return error(401, "AUTH_REQUIRED", "a valid login session is required", routed.request_id);
        return community_feed(routed);
    }

    if (routed.path == "/api/community/action")
    {
        if (routed.method != "POST")
            return error(405, "METHOD_NOT_ALLOWED", "only POST is allowed for this endpoint", routed.request_id);
        if (!routed.authenticated)
            return error(401, "AUTH_REQUIRED", "a valid login session is required", routed.request_id);
        if (!routed.csrf_valid)
            return error(403, "CSRF_REJECTED", "missing or invalid CSRF token", routed.request_id);
        return community_action(routed);
    }

    if (routed.path == "/api/community/related")
    {
        if (routed.method != "GET")
            return error(405, "METHOD_NOT_ALLOWED", "only GET is allowed for this endpoint", routed.request_id);
        if (!routed.authenticated)
            return error(401, "AUTH_REQUIRED", "a valid login session is required", routed.request_id);
        return community_related(routed);
    }

    return error(404, "API_NOT_FOUND", "the requested API endpoint does not exist", routed.request_id);
}

ApiResponse ApiRouter::community_feed(const ApiRequest &request)
{
    if (!request.community_store)
        return error(503, "COMMUNITY_STORE_UNAVAILABLE", "community database is unavailable", request.request_id);

    std::map<std::string, std::string> query;
    if (!parse_query(request.query, query))
        return error(400, "INVALID_QUERY", "query string is invalid", request.request_id);
    for (const auto &entry : query)
        if (entry.first != "cursor" && entry.first != "limit" && entry.first != "mode")
            return error(400, "INVALID_ARGUMENT", "unsupported query parameter", request.request_id);

    std::size_t limit = 10;
    if (query.count("limit"))
    {
        unsigned long long parsed = 0;
        if (!parse_unsigned_decimal(query["limit"], parsed) || parsed < 1 || parsed > 20)
            return error(400, "INVALID_ARGUMENT", "limit must be between 1 and 20", request.request_id);
        limit = static_cast<std::size_t>(parsed);
    }

    const std::string mode = query.count("mode") ? query["mode"] : "for_you";
    if (mode != "for_you" && mode != "latest")
        return error(400, "INVALID_ARGUMENT", "mode must be for_you or latest", request.request_id);

    CommunityFeedQuery feed_query;
    feed_query.limit = limit;
    feed_query.personalized = mode == "for_you";
    if (query.count("cursor"))
    {
        const std::string &cursor = query["cursor"];
        unsigned long long parsed_cursor = 0;
        const std::string expected_prefix = feed_query.personalized ? "v2:" : "v1:";
        if (cursor.compare(0, 3, expected_prefix) != 0 ||
            !parse_unsigned_decimal(cursor.substr(3), parsed_cursor) ||
            (!feed_query.personalized && parsed_cursor == 0) ||
            (feed_query.personalized && parsed_cursor > 100000))
            return error(400, "INVALID_CURSOR", "cursor is invalid for the selected mode", request.request_id);
        if (feed_query.personalized) feed_query.offset = static_cast<std::size_t>(parsed_cursor);
        else feed_query.before_id = parsed_cursor;
    }

    CommunityFeedPage page;
    std::string store_error;
    if (!request.community_store->fetch_feed(request.username, feed_query, page, store_error))
        return error(503, "COMMUNITY_STORE_ERROR", "failed to load community feed", request.request_id);

    json::array items;
    for (std::size_t index = 0; index < page.posts.size(); ++index)
    {
        const CommunityPost &post = page.posts[index];
        json::object author;
        author["username"] = post.username;
        json::object content;
        content["text"] = post.content_text;
        content["media_url"] = post.file_path.empty() ? json::value(nullptr) : json::value(post.file_path);
        content["media_type"] = post.file_type.empty() ? json::value(nullptr) : json::value(post.file_type);
        json::object stats;
        stats["likes"] = post.likes;
        stats["collects"] = post.collects;
        stats["comments"] = post.comments;
        json::object viewer;
        viewer["liked"] = post.viewer_liked;
        viewer["collected"] = post.viewer_collected;
        viewer["disliked"] = post.viewer_disliked;
        json::object recommendation;
        recommendation["reason"] = post.recommendation_reason;
        recommendation["source"] = post.recommendation_source;
        recommendation["score"] = post.recommendation_score;
        recommendation["position"] = index;
        json::object item;
        item["post_id"] = std::to_string(post.id);
        item["author"] = std::move(author);
        item["content"] = std::move(content);
        item["created_at"] = rfc3339_time(post.created_at);
        item["stats"] = std::move(stats);
        item["viewer_state"] = std::move(viewer);
        item["recommendation"] = std::move(recommendation);
        items.push_back(std::move(item));
    }

    json::object payload;
    payload["items"] = std::move(items);
    if (!page.next_cursor.empty())
        payload["next_cursor"] = page.next_cursor;
    else payload["next_cursor"] = nullptr;
    payload["request_id"] = request.request_id;
    payload["personalized"] = page.personalized && page.semantic_profile;
    payload["fallback"] = mode == "for_you" && !page.semantic_profile;
    payload["fallback_reason"] = page.fallback_reason.empty() ?
        json::value(nullptr) : json::value(page.fallback_reason);

    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = json::serialize(payload);
    response.request_id = request.request_id;
    return response;
}

ApiResponse ApiRouter::community_action(const ApiRequest &request)
{
    if (!request.community_store)
        return error(503, "COMMUNITY_STORE_UNAVAILABLE", "community database is unavailable", request.request_id);
    if (!contains_token(request.content_type, "application/json"))
        return error(415, "UNSUPPORTED_MEDIA_TYPE", "Content-Type must be application/json", request.request_id);
    if (request.body.size() > MAX_API_BODY_SIZE)
        return error(413, "PAYLOAD_TOO_LARGE", "API request body exceeds 16 KiB", request.request_id);

    json::error_code ec;
    json::value parsed = json::parse(request.body, ec);
    if (ec || !parsed.is_object())
        return error(400, "INVALID_JSON", "request body must be a valid JSON object", request.request_id);
    const json::object &object = parsed.as_object();

    const json::value *event_value = object.if_contains("event_id");
    const json::value *post_value = object.if_contains("post_id");
    const json::value *action_value = object.if_contains("action");
    if (!event_value || !event_value->is_string() || !post_value || !action_value || !action_value->is_string())
        return error(400, "INVALID_ARGUMENT", "event_id, post_id and action are required", request.request_id);

    CommunityAction action;
    action.event_id = std::string(event_value->as_string());
    action.action_type = std::string(action_value->as_string());
    if (!valid_event_id(action.event_id))
        return error(400, "INVALID_ARGUMENT", "event_id format is invalid", request.request_id);
    if (!valid_action_type(action.action_type))
        return error(400, "INVALID_ARGUMENT", "action type is unsupported", request.request_id);

    if (post_value->is_string())
    {
        if (!parse_unsigned_decimal(std::string(post_value->as_string()), action.post_id) || action.post_id == 0)
            return error(400, "INVALID_ARGUMENT", "post_id must be a positive integer string", request.request_id);
    }
    else if (post_value->is_uint64()) action.post_id = post_value->as_uint64();
    else if (post_value->is_int64() && post_value->as_int64() > 0)
        action.post_id = static_cast<unsigned long long>(post_value->as_int64());
    else return error(400, "INVALID_ARGUMENT", "post_id must be a positive integer", request.request_id);

    if (const json::value *duration = object.if_contains("duration_ms"))
    {
        if ((!duration->is_int64() && !duration->is_uint64()) ||
            (duration->is_int64() && duration->as_int64() < 0))
            return error(400, "INVALID_ARGUMENT", "duration_ms must be a non-negative integer", request.request_id);
        const std::uint64_t value = duration->is_uint64() ? duration->as_uint64() :
                                    static_cast<std::uint64_t>(duration->as_int64());
        if (value > 1800000)
            return error(400, "INVALID_ARGUMENT", "duration_ms exceeds 1800000", request.request_id);
        action.duration_ms = static_cast<unsigned int>(value);
    }
    if (action.action_type == "dwell" && action.duration_ms == 0)
        return error(400, "INVALID_ARGUMENT", "dwell requires a positive duration_ms", request.request_id);

    if (const json::value *recommendation = object.if_contains("recommendation_request_id"))
    {
        if (!recommendation->is_string() || recommendation->as_string().size() > 64)
            return error(400, "INVALID_ARGUMENT", "recommendation_request_id is invalid", request.request_id);
        action.recommendation_request_id = std::string(recommendation->as_string());
    }
    if (const json::value *position = object.if_contains("position"))
    {
        if (!position->is_int64() || position->as_int64() < 0 || position->as_int64() > 65535)
            return error(400, "INVALID_ARGUMENT", "position must be between 0 and 65535", request.request_id);
        action.position = static_cast<unsigned int>(position->as_int64());
        action.has_position = true;
    }

    CommunityActionResult result;
    std::string store_error;
    if (!request.community_store->record_action(request.username, action, result, store_error))
        return error(503, "COMMUNITY_STORE_ERROR", "failed to record community action", request.request_id);
    if (!result.post_found)
        return error(404, "POST_NOT_FOUND", "community post does not exist", request.request_id);

    json::object payload;
    payload["accepted"] = true;
    payload["duplicate"] = result.duplicate;
    payload["request_id"] = request.request_id;
    ApiResponse response;
    response.status = 200;
    response.reason = "OK";
    response.body = json::serialize(payload);
    response.request_id = request.request_id;
    return response;
}

ApiResponse ApiRouter::community_related(const ApiRequest &request)
{
    if (!request.community_store)
        return error(503, "COMMUNITY_STORE_UNAVAILABLE", "community database is unavailable", request.request_id);
    std::map<std::string, std::string> query;
    if (!parse_query(request.query, query) || query.size() != 1 || !query.count("post_id"))
        return error(400, "INVALID_ARGUMENT", "post_id is required", request.request_id);
    unsigned long long post_id = 0;
    if (!parse_unsigned_decimal(query["post_id"], post_id) || post_id == 0)
        return error(400, "INVALID_ARGUMENT", "post_id must be a positive integer", request.request_id);
    CommunityPost post;
    std::string store_error;
    if (!request.community_store->fetch_post(post_id, post, store_error))
        return error(404, "POST_NOT_FOUND", "community post does not exist", request.request_id);
    try
    {
        const auto results = rag::RagService::instance().retrieve(
            post.content_text, 8, true, true, std::to_string(post_id));
        json::array knowledge, community;
        for (const rag::SearchResult &result : results)
        {
            json::object item;
            item["source_type"] = result.chunk.source_type;
            item["source_id"] = result.chunk.source_id;
            item["title"] = result.chunk.source;
            item["author"] = result.chunk.author;
            item["text"] = result.chunk.text;
            item["score"] = result.score;
            item["trust_level"] = result.chunk.trust_level;
            if (result.chunk.source_type == "community") community.push_back(std::move(item));
            else knowledge.push_back(std::move(item));
        }
        json::object payload;
        payload["post_id"] = std::to_string(post_id);
        payload["related_knowledge"] = std::move(knowledge);
        payload["related_posts"] = std::move(community);
        payload["request_id"] = request.request_id;
        ApiResponse response;
        response.status = 200;
        response.reason = "OK";
        response.body = json::serialize(payload);
        response.request_id = request.request_id;
        return response;
    }
    catch (const std::exception &exception)
    {
        return error(502, "RELATED_RETRIEVAL_ERROR", exception.what(), request.request_id);
    }
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
    bool include_community = false;
    if (const json::value *value = object.if_contains("include_community"))
    {
        if (!value->is_bool())
            return error(400, "INVALID_ARGUMENT", "include_community must be a boolean", request_id);
        include_community = value->as_bool();
    }
    rag::RagQueryOptions query_options;
    query_options.include_community = include_community;
    query_options.username = request.username;
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
        std::thread([state, question, top_k, request_id, query_options]() {
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
                    question, static_cast<std::size_t>(top_k), query_options);
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
                sources["related_community_posts"] = community_source_documents(prepared.sources);
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
            question, static_cast<std::size_t>(top_k), query_options);
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
    payload["related_community_posts"] = community_source_documents(rag_answer.sources);
    payload["include_community"] = include_community;
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
