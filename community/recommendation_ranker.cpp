#include "recommendation_ranker.h"

#include "../ai_rag/vector_store.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace
{
double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

std::uint64_t stable_hash(const std::string &value)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value)
    {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}
}

RecommendationRanker::RecommendationRanker(RecommendationWeights weights)
    : weights_(weights) {}

std::vector<float> RecommendationRanker::build_interest_profile(
    const std::vector<InterestSignal> &signals,
    const std::vector<RecommendationCandidate> &candidates) const
{
    std::unordered_map<unsigned long long, const std::vector<float> *> embeddings;
    std::size_t dimension = 0;
    for (const RecommendationCandidate &candidate : candidates)
        if (!candidate.embedding.empty())
        {
            embeddings[candidate.post_id] = &candidate.embedding;
            dimension = candidate.embedding.size();
        }
    if (dimension == 0) return {};

    std::vector<double> accumulated(dimension, 0.0);
    double absolute_weight = 0.0;
    for (const InterestSignal &signal : signals)
    {
        const auto found = embeddings.find(signal.post_id);
        if (found == embeddings.end() || found->second->size() != dimension) continue;
        for (std::size_t index = 0; index < dimension; ++index)
            accumulated[index] += signal.weight * (*found->second)[index];
        absolute_weight += std::fabs(signal.weight);
    }
    if (absolute_weight == 0.0) return {};

    double norm = 0.0;
    for (double value : accumulated) norm += value * value;
    if (norm == 0.0) return {};
    norm = std::sqrt(norm);
    std::vector<float> profile(dimension);
    for (std::size_t index = 0; index < dimension; ++index)
        profile[index] = static_cast<float>(accumulated[index] / norm);
    return profile;
}

std::vector<RankedRecommendation> RecommendationRanker::rank(
    const std::string &username,
    const std::vector<RecommendationCandidate> &candidates,
    const std::vector<float> &interest_profile) const
{
    double max_interaction = 1.0;
    for (const RecommendationCandidate &candidate : candidates)
    {
        const double raw = candidate.likes * 3.0 + candidate.collects * 5.0 +
                           candidate.opens * 0.5;
        max_interaction = std::max(max_interaction, raw);
    }

    std::vector<RankedRecommendation> ranked;
    ranked.reserve(candidates.size());
    for (const RecommendationCandidate &candidate : candidates)
    {
        if (candidate.viewer_disliked) continue;
        RankedRecommendation item;
        item.post_id = candidate.post_id;
        if (!interest_profile.empty() && candidate.embedding.size() == interest_profile.size())
            item.semantic_score = clamp01((rag::VectorStore::cosine_similarity(
                interest_profile, candidate.embedding) + 1.0) / 2.0);
        const double raw_interaction = candidate.likes * 3.0 + candidate.collects * 5.0 +
                                       candidate.opens * 0.5;
        item.interaction_score = std::log1p(raw_interaction) / std::log1p(max_interaction);
        const double negative_ratio = static_cast<double>(candidate.dislikes + candidate.skips) /
            static_cast<double>(std::max<unsigned long long>(1, candidate.impressions));
        item.quality_score = clamp01(candidate.quality_hint - std::min(0.6, negative_ratio * 0.3));
        const double half_life = std::max(1.0, weights_.freshness_half_life_days);
        item.freshness_score = std::exp(-std::log(2.0) * std::max(0.0, candidate.age_days) / half_life);
        const std::uint64_t hash = stable_hash(username + ":" + std::to_string(candidate.post_id));
        item.exploration_score = static_cast<double>(hash % 10000) / 9999.0;
        const double penalty = std::min(0.5, candidate.viewer_impressions * weights_.seen_penalty) +
                               std::min(0.6, candidate.viewer_skips * weights_.skip_penalty);
        item.score = item.semantic_score * weights_.semantic +
                     item.interaction_score * weights_.interaction +
                     item.quality_score * weights_.quality +
                     item.freshness_score * weights_.freshness +
                     item.exploration_score * weights_.exploration - penalty;

        if (!interest_profile.empty() && item.semantic_score >= 0.65)
        {
            item.source = "semantic";
            item.reason = "与你近期的兴趣相关";
        }
        else if (item.interaction_score >= 0.55)
        {
            item.source = "popular";
            item.reason = "社区近期热门";
        }
        else if (item.freshness_score >= 0.65)
        {
            item.source = "latest";
            item.reason = "社区最新内容";
        }
        else
        {
            item.source = "explore";
            item.reason = "探索一个新话题";
        }
        ranked.push_back(std::move(item));
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedRecommendation &left,
                                                const RankedRecommendation &right) {
        if (left.score != right.score) return left.score > right.score;
        return left.post_id > right.post_id;
    });

    std::unordered_map<unsigned long long, const RecommendationCandidate *> by_id;
    for (const RecommendationCandidate &candidate : candidates) by_id[candidate.post_id] = &candidate;
    std::unordered_set<std::string> content_seen;
    std::vector<RankedRecommendation> deduplicated;
    for (RankedRecommendation &item : ranked)
    {
        const RecommendationCandidate &candidate = *by_id[item.post_id];
        if (!candidate.content_key.empty() && !content_seen.insert(candidate.content_key).second) continue;
        deduplicated.push_back(std::move(item));
    }

    for (std::size_t index = 1; index < deduplicated.size(); ++index)
    {
        const std::string &previous_author = by_id[deduplicated[index - 1].post_id]->author;
        if (by_id[deduplicated[index].post_id]->author != previous_author) continue;
        for (std::size_t next = index + 1; next < deduplicated.size(); ++next)
            if (by_id[deduplicated[next].post_id]->author != previous_author)
            {
                std::swap(deduplicated[index], deduplicated[next]);
                break;
            }
    }
    return deduplicated;
}
