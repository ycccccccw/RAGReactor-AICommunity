#ifndef RAGREACTOR_METRICS_H
#define RAGREACTOR_METRICS_H

#include <atomic>
#include <string>

class Metrics
{
public:
    static Metrics &instance();
    std::atomic<unsigned long long> api_requests{0};
    std::atomic<unsigned long long> ask_requests{0};
    std::atomic<unsigned long long> cache_hits{0};
    std::atomic<unsigned long long> cache_misses{0};
    std::atomic<unsigned long long> rate_limited{0};
    std::atomic<unsigned long long> upstream_failures{0};
    std::atomic<unsigned long long> circuit_rejections{0};
    std::atomic<unsigned long long> rerank_failures{0};
    std::atomic<unsigned long long> stream_active{0};
    std::atomic<unsigned long long> recommendation_requests{0};
    std::atomic<unsigned long long> recommendation_duration_us{0};
    std::atomic<unsigned long long> recommendation_candidates{0};
    std::atomic<unsigned long long> recommendation_fallbacks{0};
    std::atomic<unsigned long long> recommendation_embedding_failures{0};
    std::atomic<unsigned long long> recommendation_snapshot_hits{0};
    std::string json_snapshot() const;
};

#endif
