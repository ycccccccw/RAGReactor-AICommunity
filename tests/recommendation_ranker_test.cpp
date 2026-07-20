#include "../community/recommendation_ranker.h"

#include <cassert>
#include <iostream>

namespace
{
RecommendationCandidate candidate(unsigned long long id, const std::string &author,
                                  std::vector<float> embedding)
{
    RecommendationCandidate value;
    value.post_id = id;
    value.author = author;
    value.content_key = "content-" + std::to_string(id);
    value.embedding = std::move(embedding);
    value.age_days = 2.0;
    value.quality_hint = 0.8;
    return value;
}
}

int main()
{
    RecommendationWeights semantic_weights;
    semantic_weights.semantic = 1.0;
    semantic_weights.interaction = 0.0;
    semantic_weights.quality = 0.0;
    semantic_weights.freshness = 0.0;
    semantic_weights.exploration = 0.0;
    semantic_weights.seen_penalty = 0.08;
    semantic_weights.skip_penalty = 0.15;
    RecommendationRanker semantic_ranker(semantic_weights);

    std::vector<RecommendationCandidate> candidates;
    candidates.push_back(candidate(1, "alice", {1.0f, 0.0f, 0.0f}));
    candidates.back().viewer_impressions = 10;
    candidates.push_back(candidate(2, "bob", {0.98f, 0.02f, 0.0f}));
    candidates.push_back(candidate(3, "carol", {0.0f, 1.0f, 0.0f}));
    candidates.push_back(candidate(4, "dave", {0.0f, 0.98f, 0.02f}));

    const std::vector<float> profile_a = semantic_ranker.build_interest_profile({{1, 3.0}}, candidates);
    const std::vector<float> profile_b = semantic_ranker.build_interest_profile({{3, 5.0}}, candidates);
    assert(!profile_a.empty() && !profile_b.empty());
    const auto ranked_a = semantic_ranker.rank("user-a", candidates, profile_a);
    const auto ranked_b = semantic_ranker.rank("user-b", candidates, profile_b);
    assert(ranked_a.front().post_id == 2);
    assert(ranked_b.front().post_id == 3 || ranked_b.front().post_id == 4);
    assert(ranked_a.front().post_id != ranked_b.front().post_id);

    candidates[1].viewer_disliked = true;
    const auto disliked = semantic_ranker.rank("user-a", candidates, profile_a);
    for (const auto &item : disliked) assert(item.post_id != 2);

    RecommendationWeights cold_weights;
    RecommendationRanker cold_ranker(cold_weights);
    candidates[1].viewer_disliked = false;
    candidates[3].likes = 20;
    candidates[3].collects = 10;
    const auto cold_first = cold_ranker.rank("new-user", candidates, {});
    const auto cold_second = cold_ranker.rank("new-user", candidates, {});
    assert(!cold_first.empty());
    assert(cold_first.front().post_id == cold_second.front().post_id);
    assert(cold_first.front().post_id == 4);

    std::vector<RecommendationCandidate> duplicate_authors;
    duplicate_authors.push_back(candidate(10, "same", {1.0f, 0.0f}));
    duplicate_authors.push_back(candidate(11, "same", {0.99f, 0.01f}));
    duplicate_authors.push_back(candidate(12, "different", {0.98f, 0.02f}));
    const auto author_ranked = semantic_ranker.rank("author-test", duplicate_authors, {1.0f, 0.0f});
    assert(author_ranked.size() == 3);
    assert(author_ranked[0].post_id != author_ranked[1].post_id);
    assert(duplicate_authors[author_ranked[0].post_id == 10 ? 0 : 1].author == "same");
    assert(author_ranked[1].post_id == 12);

    duplicate_authors[2].content_key = duplicate_authors[0].content_key;
    const auto content_dedup = semantic_ranker.rank("dedup-test", duplicate_authors, {1.0f, 0.0f});
    assert(content_dedup.size() == 2);

    std::cout << "recommendation_ranker_test: all checks passed\n";
    return 0;
}
