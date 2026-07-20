#ifndef RAGREACTOR_RECOMMENDATION_RANKER_H
#define RAGREACTOR_RECOMMENDATION_RANKER_H

#include <string>
#include <vector>

struct RecommendationWeights
{
    double semantic = 0.45;
    double interaction = 0.20;
    double quality = 0.15;
    double freshness = 0.10;
    double exploration = 0.10;
    double seen_penalty = 0.08;
    double skip_penalty = 0.15;
    double freshness_half_life_days = 14.0;
};

struct InterestSignal
{
    unsigned long long post_id = 0;
    double weight = 0.0;
};

struct RecommendationCandidate
{
    unsigned long long post_id = 0;
    std::string author;
    std::string content_key;
    std::vector<float> embedding;
    unsigned long long likes = 0;
    unsigned long long collects = 0;
    unsigned long long opens = 0;
    unsigned long long impressions = 0;
    unsigned long long skips = 0;
    unsigned long long dislikes = 0;
    unsigned long long viewer_impressions = 0;
    unsigned long long viewer_skips = 0;
    double age_days = 0.0;
    double quality_hint = 0.0;
    bool viewer_disliked = false;
};

struct RankedRecommendation
{
    unsigned long long post_id = 0;
    double score = 0.0;
    double semantic_score = 0.0;
    double interaction_score = 0.0;
    double quality_score = 0.0;
    double freshness_score = 0.0;
    double exploration_score = 0.0;
    std::string source;
    std::string reason;
};

class RecommendationRanker
{
public:
    explicit RecommendationRanker(RecommendationWeights weights);

    std::vector<float> build_interest_profile(
        const std::vector<InterestSignal> &signals,
        const std::vector<RecommendationCandidate> &candidates) const;

    std::vector<RankedRecommendation> rank(
        const std::string &username,
        const std::vector<RecommendationCandidate> &candidates,
        const std::vector<float> &interest_profile) const;

private:
    RecommendationWeights weights_;
};

#endif
