#ifndef RAGREACTOR_COMMUNITY_STORE_H
#define RAGREACTOR_COMMUNITY_STORE_H

#include <cstddef>
#include <string>
#include <vector>

struct CommunityPost
{
    unsigned long long id = 0;
    std::string username;
    std::string content_text;
    std::string file_path;
    std::string file_type;
    std::string created_at;
    unsigned long long likes = 0;
    unsigned long long collects = 0;
    unsigned long long comments = 0;
    bool viewer_liked = false;
    bool viewer_collected = false;
    bool viewer_disliked = false;
    std::string recommendation_source = "latest";
    std::string recommendation_reason = "最新发布";
    double recommendation_score = 0.0;
};

struct CommunityFeedQuery
{
    std::size_t limit = 10;
    bool personalized = false;
    unsigned long long before_id = 0;
    std::size_t offset = 0;
};

struct CommunityFeedPage
{
    std::vector<CommunityPost> posts;
    bool has_more = false;
    std::string next_cursor;
    bool personalized = false;
    bool semantic_profile = false;
    std::string fallback_reason;
};

struct CommunityAction
{
    std::string event_id;
    unsigned long long post_id = 0;
    std::string action_type;
    unsigned int duration_ms = 0;
    std::string recommendation_request_id;
    unsigned int position = 0;
    bool has_position = false;
};

struct CommunityActionResult
{
    bool duplicate = false;
    bool post_found = true;
};

class CommunityStore
{
public:
    virtual ~CommunityStore() = default;

    virtual bool fetch_feed(const std::string &username,
                            const CommunityFeedQuery &query,
                            CommunityFeedPage &page,
                            std::string &error) = 0;

    virtual bool record_action(const std::string &username,
                               const CommunityAction &action,
                               CommunityActionResult &result,
                               std::string &error) = 0;

    virtual bool fetch_post(unsigned long long post_id, CommunityPost &post,
                            std::string &error) = 0;
};

#endif
