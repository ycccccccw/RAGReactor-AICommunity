#include "mysql_community_store.h"
#include "recommendation_ranker.h"
#include "../ai_rag/vector_store.h"
#include "question_interest_store.h"
#include "../api/metrics.h"

#include <boost/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace
{
struct FeedSnapshot
{
    CommunityFeedPage page;
    std::chrono::steady_clock::time_point expires;
};
std::mutex snapshot_mutex;
std::unordered_map<std::string, FeedSnapshot> feed_snapshots;

std::string snapshot_key(const std::string &username, const CommunityFeedQuery &query)
{
    return username + ":" + std::to_string(query.offset) + ":" + std::to_string(query.limit);
}

void invalidate_snapshot(const std::string &username)
{
    std::lock_guard<std::mutex> guard(snapshot_mutex);
    const std::string prefix = username + ":";
    for (auto it = feed_snapshots.begin(); it != feed_snapshots.end();)
        if (it->first.compare(0, prefix.size(), prefix) == 0) it = feed_snapshots.erase(it);
        else ++it;
}

std::string escape_sql(MYSQL *connection, const std::string &value)
{
    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long length = mysql_real_escape_string(
        connection, &escaped[0], value.data(), static_cast<unsigned long>(value.size()));
    escaped.resize(length);
    return escaped;
}

unsigned long long unsigned_value(const char *value)
{
    return value ? std::strtoull(value, nullptr, 10) : 0;
}

bool execute(MYSQL *connection, const std::string &sql, std::string &error)
{
    if (mysql_query(connection, sql.c_str()) == 0) return true;
    error = mysql_error(connection);
    return false;
}

bool begin_transaction(MYSQL *connection, std::string &error)
{
    return execute(connection, "START TRANSACTION", error);
}

void rollback_transaction(MYSQL *connection)
{
    mysql_rollback(connection);
}

double env_double(const char *name, double fallback)
{
    const char *value = std::getenv(name);
    if (!value || !value[0]) return fallback;
    try { return std::stod(value); }
    catch (...) { return fallback; }
}

std::string env_string(const char *name, const std::string &fallback)
{
    const char *value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

std::string content_key(const CommunityPost &post)
{
    std::string key;
    for (unsigned char ch : post.content_text)
        if (!std::isspace(ch)) key.push_back(static_cast<char>(std::tolower(ch)));
    key += "|" + post.file_path;
    return key;
}

using PostEmbeddings = std::unordered_map<unsigned long long, std::vector<float>>;

std::shared_ptr<const PostEmbeddings> load_post_embeddings(
    const std::string &path, std::size_t &dimension)
{
    struct Cache
    {
        std::string path;
        std::filesystem::file_time_type modified;
        std::size_t dimension = 0;
        std::shared_ptr<const PostEmbeddings> embeddings;
    };
    static std::mutex cache_mutex;
    static Cache cache;
    std::error_code filesystem_error;
    const auto modified = std::filesystem::last_write_time(path, filesystem_error);
    {
        std::lock_guard<std::mutex> guard(cache_mutex);
        if (!filesystem_error && cache.embeddings && cache.path == path &&
            cache.modified == modified)
        {
            dimension = cache.dimension;
            return cache.embeddings;
        }
    }
    std::unordered_map<unsigned long long, std::vector<double>> accumulated;
    std::unordered_map<unsigned long long, std::size_t> counts;
    rag::VectorStore store;
    std::string error;
    if (!store.load(path, &error)) return std::make_shared<const PostEmbeddings>();
    dimension = store.dimension();
    for (const rag::DocumentChunk &chunk : store.chunks())
    {
        if (chunk.source_type != "community" || chunk.status != "ready") continue;
        char *end = nullptr;
        const unsigned long long post_id = std::strtoull(chunk.source_id.c_str(), &end, 10);
        if (!end || *end || post_id == 0 || chunk.embedding.size() != dimension) continue;
        std::vector<double> &sum = accumulated[post_id];
        if (sum.empty()) sum.resize(dimension, 0.0);
        for (std::size_t index = 0; index < dimension; ++index) sum[index] += chunk.embedding[index];
        ++counts[post_id];
    }
    PostEmbeddings embeddings;
    for (auto &entry : accumulated)
    {
        double norm = 0.0;
        for (double value : entry.second) norm += value * value;
        if (norm == 0.0) continue;
        norm = std::sqrt(norm);
        std::vector<float> vector(dimension);
        for (std::size_t index = 0; index < dimension; ++index)
            vector[index] = static_cast<float>(entry.second[index] / norm);
        embeddings[entry.first] = std::move(vector);
    }
    auto loaded = std::make_shared<const PostEmbeddings>(std::move(embeddings));
    if (!filesystem_error)
    {
        std::lock_guard<std::mutex> guard(cache_mutex);
        cache = {path, modified, dimension, loaded};
    }
    return loaded;
}

bool persist_profile(MYSQL *connection, const std::string &username,
                     const std::vector<float> &profile, std::size_t signal_count,
                     std::string &error)
{
    boost::json::array values;
    values.reserve(profile.size());
    for (float value : profile) values.push_back(value);
    const std::string json = profile.empty() ? "" : boost::json::serialize(values);
    const std::string summary = "signals=" + std::to_string(signal_count);
    std::ostringstream sql;
    sql << "INSERT INTO user_interest_profiles"
        << "(username,profile_version,embedding_model,embedding_dimension,embedding_json,interest_summary) VALUES('"
        << escape_sql(connection, username) << "',1,'community-profile-v1',";
    if (profile.empty()) sql << "NULL,NULL";
    else sql << profile.size() << ",'" << escape_sql(connection, json) << '\'';
    sql << ",'" << escape_sql(connection, summary) << "') ON DUPLICATE KEY UPDATE "
        << "profile_version=profile_version+1,embedding_model=VALUES(embedding_model),"
        << "embedding_dimension=VALUES(embedding_dimension),embedding_json=VALUES(embedding_json),"
        << "interest_summary=VALUES(interest_summary)";
    return execute(connection, sql.str(), error);
}
}

bool MysqlCommunityStore::fetch_feed(const std::string &username,
                                     const CommunityFeedQuery &query,
                                     CommunityFeedPage &page,
                                     std::string &error)
{
    const auto recommendation_started = std::chrono::steady_clock::now();
    page = CommunityFeedPage();
    if (!connection_)
    {
        error = "database connection is unavailable";
        return false;
    }
    if (query.personalized)
    {
        std::lock_guard<std::mutex> guard(snapshot_mutex);
        const auto found = feed_snapshots.find(snapshot_key(username, query));
        if (found != feed_snapshots.end() && found->second.expires > std::chrono::steady_clock::now())
        {
            page = found->second.page;
            Metrics::instance().recommendation_snapshot_hits.fetch_add(1);
            return true;
        }
    }

    const std::string escaped_username = escape_sql(connection_, username);
    std::ostringstream sql;
    sql << "SELECT p.id,p.username,p.content_text,p.file_path,p.file_type,p.created_at,"
        << "COALESCE(s.likes,0),COALESCE(s.collects,0),0,"
        << "COALESCE(us.liked,0),COALESCE(us.collected,0),COALESCE(us.disliked,0),"
        << "COALESCE(s.opens,0),COALESCE(s.impressions,0),COALESCE(s.skips,0),"
        << "COALESCE(s.dislikes,0),GREATEST(0,TIMESTAMPDIFF(HOUR,p.created_at,NOW())/24.0),"
        << "(SELECT COUNT(*) FROM community_actions va WHERE va.username='" << escaped_username
        << "' AND va.post_id=p.id AND va.action_type='impression'),"
        << "(SELECT COUNT(*) FROM community_actions vs WHERE vs.username='" << escaped_username
        << "' AND vs.post_id=p.id AND vs.action_type='skip') "
        << "FROM user_posts p "
        << "LEFT JOIN community_post_stats s ON s.post_id=p.id "
        << "LEFT JOIN community_user_post_state us ON us.post_id=p.id AND us.username='"
        << escaped_username << "' ";
    if (!query.personalized && query.before_id > 0) sql << "WHERE p.id < " << query.before_id << ' ';
    sql << "ORDER BY p.id DESC LIMIT " << (query.personalized ? 500 : query.limit + 1);

    if (!execute(connection_, sql.str(), error)) return false;
    MYSQL_RES *result = mysql_store_result(connection_);
    if (!result)
    {
        error = mysql_error(connection_);
        return false;
    }

    struct RankMeta
    {
        unsigned long long opens = 0;
        unsigned long long impressions = 0;
        unsigned long long skips = 0;
        unsigned long long dislikes = 0;
        double age_days = 0.0;
        unsigned long long viewer_impressions = 0;
        unsigned long long viewer_skips = 0;
    };
    std::unordered_map<unsigned long long, RankMeta> rank_meta;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr)
    {
        if (!query.personalized && page.posts.size() == query.limit)
        {
            page.has_more = true;
            break;
        }
        CommunityPost post;
        post.id = unsigned_value(row[0]);
        post.username = row[1] ? row[1] : "";
        post.content_text = row[2] ? row[2] : "";
        post.file_path = row[3] ? row[3] : "";
        post.file_type = row[4] ? row[4] : "";
        post.created_at = row[5] ? row[5] : "";
        post.likes = unsigned_value(row[6]);
        post.collects = unsigned_value(row[7]);
        post.comments = unsigned_value(row[8]);
        post.viewer_liked = unsigned_value(row[9]) != 0;
        post.viewer_collected = unsigned_value(row[10]) != 0;
        post.viewer_disliked = unsigned_value(row[11]) != 0;
        post.recommendation_score = 0.0;
        RankMeta meta;
        meta.opens = unsigned_value(row[12]);
        meta.impressions = unsigned_value(row[13]);
        meta.skips = unsigned_value(row[14]);
        meta.dislikes = unsigned_value(row[15]);
        meta.age_days = row[16] ? std::strtod(row[16], nullptr) : 0.0;
        meta.viewer_impressions = unsigned_value(row[17]);
        meta.viewer_skips = unsigned_value(row[18]);
        rank_meta[post.id] = meta;
        page.posts.push_back(std::move(post));
    }
    mysql_free_result(result);

    if (!query.personalized)
    {
        if (page.has_more && !page.posts.empty())
            page.next_cursor = "v1:" + std::to_string(page.posts.back().id);
        return true;
    }

    page.personalized = true;
    std::size_t embedding_dimension = 0;
    const auto embeddings = load_post_embeddings(env_string("COMMUNITY_INDEX_PATH",
        "knowledge/index/community-bailian-v4-1024.ragvec"), embedding_dimension);
    std::vector<RecommendationCandidate> candidates;
    candidates.reserve(page.posts.size());
    std::unordered_map<unsigned long long, CommunityPost> posts;
    for (const CommunityPost &post : page.posts)
    {
        RecommendationCandidate candidate;
        candidate.post_id = post.id;
        candidate.author = post.username;
        candidate.content_key = content_key(post);
        const auto embedding = embeddings->find(post.id);
        if (embedding != embeddings->end()) candidate.embedding = embedding->second;
        candidate.likes = post.likes;
        candidate.collects = post.collects;
        const RankMeta &meta = rank_meta[post.id];
        candidate.opens = meta.opens;
        candidate.impressions = meta.impressions;
        candidate.skips = meta.skips;
        candidate.dislikes = meta.dislikes;
        candidate.viewer_impressions = meta.viewer_impressions;
        candidate.viewer_skips = meta.viewer_skips;
        candidate.age_days = meta.age_days;
        candidate.quality_hint = std::min(0.7, post.content_text.size() / 300.0 * 0.7) +
                                 (post.file_path.empty() ? 0.0 : 0.3);
        candidate.viewer_disliked = post.viewer_disliked;
        candidates.push_back(std::move(candidate));
        posts[post.id] = post;
    }

    std::vector<InterestSignal> signals;
    for (const CommunityPost &post : page.posts)
    {
        if (post.viewer_liked) signals.push_back({post.id, 3.0});
        if (post.viewer_collected) signals.push_back({post.id, 5.0});
        if (post.viewer_disliked) signals.push_back({post.id, -5.0});
    }
    std::ostringstream actions_sql;
    actions_sql << "SELECT post_id,action_type,duration_ms,TIMESTAMPDIFF(DAY,created_at,NOW()) "
                << "FROM community_actions WHERE username='" << escaped_username
                << "' AND created_at>=NOW()-INTERVAL 90 DAY ORDER BY id DESC LIMIT 1000";
    if (execute(connection_, actions_sql.str(), error))
    {
        MYSQL_RES *actions = mysql_store_result(connection_);
        while (actions && (row = mysql_fetch_row(actions)) != nullptr)
        {
            const unsigned long long post_id = unsigned_value(row[0]);
            const std::string type = row[1] ? row[1] : "";
            const double age_factor = unsigned_value(row[3]) <= 7 ? 1.0 : 0.25;
            double weight = 0.0;
            if (type == "dwell") weight = std::min(3.0, unsigned_value(row[2]) / 10000.0);
            else if (type == "open") weight = 0.5;
            else if (type == "skip") weight = -1.0;
            if (weight != 0.0) signals.push_back({post_id, weight * age_factor});
        }
        if (actions) mysql_free_result(actions);
    }
    else return false;

    RecommendationWeights weights;
    weights.semantic = env_double("COMMUNITY_WEIGHT_SEMANTIC", 0.45);
    weights.interaction = env_double("COMMUNITY_WEIGHT_INTERACTION", 0.20);
    weights.quality = env_double("COMMUNITY_WEIGHT_QUALITY", 0.15);
    weights.freshness = env_double("COMMUNITY_WEIGHT_FRESHNESS", 0.10);
    weights.exploration = env_double("COMMUNITY_WEIGHT_EXPLORATION", 0.10);
    weights.seen_penalty = env_double("COMMUNITY_SEEN_PENALTY", 0.08);
    weights.skip_penalty = env_double("COMMUNITY_SKIP_PENALTY", 0.15);
    weights.freshness_half_life_days = env_double("COMMUNITY_FRESHNESS_HALF_LIFE_DAYS", 14.0);
    RecommendationRanker ranker(weights);
    const std::vector<float> profile = ranker.build_interest_profile(signals, candidates);
    std::vector<float> blended_profile = profile;
    const std::vector<float> question_profile = QuestionInterestStore::instance().current(username);
    if (!question_profile.empty() && (blended_profile.empty() || blended_profile.size() == question_profile.size()))
    {
        const double question_weight = std::max(0.0, std::min(1.0,
            env_double("COMMUNITY_QUESTION_INTEREST_WEIGHT", 0.20)));
        if (blended_profile.empty()) blended_profile = question_profile;
        else
        {
            double norm = 0.0;
            for (std::size_t i = 0; i < blended_profile.size(); ++i)
            {
                blended_profile[i] = static_cast<float>(
                    blended_profile[i] * (1.0 - question_weight) + question_profile[i] * question_weight);
                norm += blended_profile[i] * blended_profile[i];
            }
            if (norm > 0.0)
            {
                norm = std::sqrt(norm);
                for (float &value : blended_profile) value = static_cast<float>(value / norm);
            }
        }
    }
    if (!persist_profile(connection_, username, blended_profile,
                         signals.size() + (question_profile.empty() ? 0 : 1), error)) return false;
    page.semantic_profile = !blended_profile.empty();
    if (embeddings->empty()) page.fallback_reason = "community_index_unavailable";
    else if (profile.empty()) page.fallback_reason = "cold_start";

    const std::vector<RankedRecommendation> ranked = ranker.rank(username, candidates, blended_profile);
    page.posts.clear();
    const std::size_t end = std::min(ranked.size(), query.offset + query.limit);
    for (std::size_t index = query.offset; index < end; ++index)
    {
        CommunityPost post = posts[ranked[index].post_id];
        post.recommendation_source = ranked[index].source;
        post.recommendation_reason = ranked[index].reason;
        post.recommendation_score = ranked[index].score;
        page.posts.push_back(std::move(post));
    }
    page.has_more = end < ranked.size();
    if (page.has_more) page.next_cursor = "v2:" + std::to_string(end);
    Metrics &metrics = Metrics::instance();
    metrics.recommendation_requests.fetch_add(1);
    metrics.recommendation_candidates.fetch_add(candidates.size());
    metrics.recommendation_duration_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - recommendation_started).count());
    if (!page.fallback_reason.empty()) metrics.recommendation_fallbacks.fetch_add(1);
    if (embeddings->empty()) metrics.recommendation_embedding_failures.fetch_add(1);
    const double snapshot_seconds = std::max(0.0, env_double("COMMUNITY_FEED_SNAPSHOT_SECONDS", 10.0));
    if (snapshot_seconds > 0.0)
    {
        std::lock_guard<std::mutex> guard(snapshot_mutex);
        feed_snapshots[snapshot_key(username, query)] = {
            page, std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(static_cast<long long>(snapshot_seconds * 1000.0))};
    }
    return true;
}

bool MysqlCommunityStore::fetch_post(unsigned long long post_id, CommunityPost &post,
                                     std::string &error)
{
    if (!connection_ || post_id == 0) { error = "invalid post lookup"; return false; }
    std::ostringstream sql;
    sql << "SELECT id,username,content_text,file_path,file_type,created_at "
           "FROM user_posts WHERE id=" << post_id << " LIMIT 1";
    if (!execute(connection_, sql.str(), error)) return false;
    MYSQL_RES *result = mysql_store_result(connection_);
    MYSQL_ROW row = result ? mysql_fetch_row(result) : nullptr;
    if (!row) { if (result) mysql_free_result(result); error = "post not found"; return false; }
    post.id = unsigned_value(row[0]);
    post.username = row[1] ? row[1] : "";
    post.content_text = row[2] ? row[2] : "";
    post.file_path = row[3] ? row[3] : "";
    post.file_type = row[4] ? row[4] : "";
    post.created_at = row[5] ? row[5] : "";
    mysql_free_result(result);
    return true;
}

bool MysqlCommunityStore::record_action(const std::string &username,
                                        const CommunityAction &action,
                                        CommunityActionResult &result,
                                        std::string &error)
{
    result = CommunityActionResult();
    if (!connection_)
    {
        error = "database connection is unavailable";
        return false;
    }

    const std::string user = escape_sql(connection_, username);
    const std::string event = escape_sql(connection_, action.event_id);
    const std::string type = escape_sql(connection_, action.action_type);
    const std::string recommendation = escape_sql(connection_, action.recommendation_request_id);

    if (!begin_transaction(connection_, error)) return false;

    std::ostringstream post_check;
    post_check << "SELECT id FROM user_posts WHERE id=" << action.post_id << " FOR SHARE";
    if (!execute(connection_, post_check.str(), error))
    {
        rollback_transaction(connection_);
        return false;
    }
    MYSQL_RES *post_result = mysql_store_result(connection_);
    if (!post_result || mysql_num_rows(post_result) == 0)
    {
        if (post_result) mysql_free_result(post_result);
        rollback_transaction(connection_);
        result.post_found = false;
        return true;
    }
    mysql_free_result(post_result);

    std::ostringstream insert;
    insert << "INSERT IGNORE INTO community_actions"
           << "(event_id,username,post_id,action_type,duration_ms,recommendation_request_id,position,occurred_at) VALUES('"
           << event << "','" << user << "'," << action.post_id << ",'" << type << "',"
           << action.duration_ms << ',';
    if (recommendation.empty()) insert << "NULL";
    else insert << '\'' << recommendation << '\'';
    insert << ',';
    if (action.has_position) insert << action.position;
    else insert << "NULL";
    insert << ",CURRENT_TIMESTAMP)";

    if (!execute(connection_, insert.str(), error))
    {
        rollback_transaction(connection_);
        return false;
    }
    if (mysql_affected_rows(connection_) == 0)
    {
        result.duplicate = true;
        return mysql_commit(connection_) == 0;
    }

    std::ostringstream ensure_stats;
    ensure_stats << "INSERT IGNORE INTO community_post_stats(post_id) VALUES(" << action.post_id << ')';
    if (!execute(connection_, ensure_stats.str(), error))
    {
        rollback_transaction(connection_);
        return false;
    }

    std::ostringstream ensure_state;
    ensure_state << "INSERT IGNORE INTO community_user_post_state(username,post_id) VALUES('"
                 << user << "'," << action.post_id << ')';
    if (!execute(connection_, ensure_state.str(), error))
    {
        rollback_transaction(connection_);
        return false;
    }

    std::ostringstream state_query;
    state_query << "SELECT liked,collected,disliked FROM community_user_post_state WHERE username='"
                << user << "' AND post_id=" << action.post_id << " FOR UPDATE";
    if (!execute(connection_, state_query.str(), error))
    {
        rollback_transaction(connection_);
        return false;
    }
    MYSQL_RES *state_result = mysql_store_result(connection_);
    MYSQL_ROW state_row = state_result ? mysql_fetch_row(state_result) : nullptr;
    if (!state_row)
    {
        if (state_result) mysql_free_result(state_result);
        error = "failed to lock community state";
        rollback_transaction(connection_);
        return false;
    }
    const bool was_liked = unsigned_value(state_row[0]) != 0;
    const bool was_collected = unsigned_value(state_row[1]) != 0;
    const bool was_disliked = unsigned_value(state_row[2]) != 0;
    mysql_free_result(state_result);

    std::string stats_update;
    std::string state_update;
    if (action.action_type == "impression") stats_update = "impressions=impressions+1";
    else if (action.action_type == "open") stats_update = "opens=opens+1";
    else if (action.action_type == "dwell")
        stats_update = "dwell_ms=dwell_ms+" + std::to_string(action.duration_ms);
    else if (action.action_type == "skip") stats_update = "skips=skips+1";
    else if (action.action_type == "like")
    {
        if (!was_liked) stats_update = "likes=likes+1";
        state_update = "liked=1";
    }
    else if (action.action_type == "unlike")
    {
        if (was_liked) stats_update = "likes=GREATEST(likes-1,0)";
        state_update = "liked=0";
    }
    else if (action.action_type == "collect")
    {
        if (!was_collected) stats_update = "collects=collects+1";
        state_update = "collected=1";
    }
    else if (action.action_type == "uncollect")
    {
        if (was_collected) stats_update = "collects=GREATEST(collects-1,0)";
        state_update = "collected=0";
    }
    else if (action.action_type == "dislike")
    {
        if (!was_disliked) stats_update = "dislikes=dislikes+1";
        state_update = "disliked=1";
    }

    if (!stats_update.empty())
    {
        const std::string sql = "UPDATE community_post_stats SET " + stats_update +
                                " WHERE post_id=" + std::to_string(action.post_id);
        if (!execute(connection_, sql, error))
        {
            rollback_transaction(connection_);
            return false;
        }
    }
    if (!state_update.empty())
    {
        const std::string sql = "UPDATE community_user_post_state SET " + state_update +
                                " WHERE username='" + user + "' AND post_id=" +
                                std::to_string(action.post_id);
        if (!execute(connection_, sql, error))
        {
            rollback_transaction(connection_);
            return false;
        }
    }

    if (mysql_commit(connection_) != 0)
    {
        error = mysql_error(connection_);
        rollback_transaction(connection_);
        return false;
    }
    if (action.action_type == "like" || action.action_type == "unlike" ||
        action.action_type == "collect" || action.action_type == "uncollect" ||
        action.action_type == "dislike")
        invalidate_snapshot(username);
    return true;
}
