#include "../api/api_router.h"
#include "../api/sse_stream.h"
#include "../api/community_store.h"

#include <boost/json.hpp>
#include <cassert>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace json = boost::json;

class FakeCommunityStore : public CommunityStore
{
public:
    unsigned long long received_before = 0;
    std::size_t received_offset = 0;
    std::size_t received_limit = 0;
    std::string received_username;
    CommunityAction received_action;
    unsigned long long received_post_id = 0;

    bool fetch_feed(const std::string &username, const CommunityFeedQuery &query,
                    CommunityFeedPage &page, std::string &) override
    {
        received_username = username;
        received_before = query.before_id;
        received_offset = query.offset;
        received_limit = query.limit;
        CommunityPost first;
        first.id = query.before_id ? query.before_id - 1 : 12;
        first.username = "alice";
        first.content_text = "post";
        first.created_at = "2026-07-20 08:30:00";
        page.posts.push_back(first);
        page.has_more = true;
        page.personalized = query.personalized;
        page.semantic_profile = query.personalized;
        page.next_cursor = query.personalized ? "v2:1" : "v1:12";
        return true;
    }

    bool record_action(const std::string &username, const CommunityAction &action,
                       CommunityActionResult &result, std::string &) override
    {
        received_username = username;
        received_action = action;
        result.duplicate = action.event_id == "duplicate-event-0001";
        return true;
    }

    bool fetch_post(unsigned long long post_id, CommunityPost &post,
                    std::string &) override
    {
        received_post_id = post_id;
        post.id = post_id;
        post.content_text = "reactor test post";
        return true;
    }
};

int main()
{
    unsetenv("BAILIAN_API_KEY");
    unsetenv("BAILIAN_BASE_URL");
    ApiRequest health_request;
    health_request.method = "GET";
    health_request.path = "/api/health";
    ApiResponse health = ApiRouter::route(health_request);
    assert(health.status == 200);
    assert(!health.request_id.empty());
    assert(json::parse(health.body).as_object().at("status") == "ok");
    assert(json::parse(health.body).as_object().contains("semantic_cache_entries"));

    ApiRequest metrics_request;
    metrics_request.method = "GET";
    metrics_request.path = "/api/metrics";
    assert(ApiRouter::route(metrics_request).status == 401);
    metrics_request.authenticated = true;
    ApiResponse metrics = ApiRouter::route(metrics_request);
    assert(metrics.status == 200);
    assert(json::parse(metrics.body).as_object().contains("api_requests_total"));
    assert(json::parse(metrics.body).as_object().contains("recommendation_duration_ms_average"));
    assert(json::parse(metrics.body).as_object().contains("recommendation_snapshot_hits_total"));

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
    assert(json::parse(invalid_json.body).as_object().contains("request_id"));

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

    FakeCommunityStore community_store;
    ApiRequest feed_request;
    feed_request.method = "GET";
    feed_request.path = "/api/community/feed";
    assert(ApiRouter::route(feed_request).status == 401);
    feed_request.authenticated = true;
    feed_request.username = "reader";
    feed_request.community_store = &community_store;
    feed_request.query = "limit=5&mode=latest";
    ApiResponse feed = ApiRouter::route(feed_request);
    assert(feed.status == 200);
    assert(community_store.received_limit == 5);
    assert(community_store.received_username == "reader");
    json::object feed_body = json::parse(feed.body).as_object();
    assert(feed_body.at("items").as_array().size() == 1);
    assert(feed_body.at("next_cursor") == "v1:12");
    assert(feed_body.at("fallback") == false);

    feed_request.query = "cursor=v1%3A12&limit=5&mode=latest";
    assert(ApiRouter::route(feed_request).status == 200);
    assert(community_store.received_before == 12);
    feed_request.query = "cursor=bad";
    assert(ApiRouter::route(feed_request).status == 400);
    feed_request.query = "limit=21";
    assert(ApiRouter::route(feed_request).status == 400);
    feed_request.query = "mode=for_you&cursor=v2%3A10&limit=5";
    ApiResponse personalized = ApiRouter::route(feed_request);
    assert(personalized.status == 200);
    assert(community_store.received_offset == 10);
    assert(json::parse(personalized.body).as_object().at("personalized") == true);

    ApiRequest action_request;
    action_request.method = "POST";
    action_request.path = "/api/community/action";
    action_request.content_type = "application/json";
    action_request.body = R"({"event_id":"action-event-0001","post_id":"12","action":"like"})";
    assert(ApiRouter::route(action_request).status == 401);
    action_request.authenticated = true;
    assert(ApiRouter::route(action_request).status == 403);
    action_request.csrf_valid = true;
    action_request.username = "reader";
    action_request.community_store = &community_store;
    ApiResponse action = ApiRouter::route(action_request);
    assert(action.status == 200);
    assert(community_store.received_action.post_id == 12);
    assert(community_store.received_action.action_type == "like");
    assert(community_store.received_username == "reader");

    action_request.body = R"({"event_id":"short","post_id":"12","action":"like"})";
    assert(ApiRouter::route(action_request).status == 400);
    action_request.body = R"({"event_id":"action-event-0002","post_id":"12","action":"dwell","duration_ms":0})";
    assert(ApiRouter::route(action_request).status == 400);
    action_request.body = R"({"event_id":"action-event-0003","post_id":"12","action":"unknown"})";
    assert(ApiRouter::route(action_request).status == 400);

    ApiRequest related_request;
    related_request.method = "GET";
    related_request.path = "/api/community/related";
    related_request.authenticated = true;
    related_request.community_store = &community_store;
    related_request.query = "post_id=bad";
    assert(ApiRouter::route(related_request).status == 400);

    std::cout << "api_router_test: all checks passed\n";
    return 0;
}
