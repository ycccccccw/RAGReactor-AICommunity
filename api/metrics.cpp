#include "metrics.h"
#include <boost/json.hpp>

Metrics &Metrics::instance()
{
    static Metrics metrics;
    return metrics;
}

std::string Metrics::json_snapshot() const
{
    boost::json::object value;
    value["api_requests_total"] = api_requests.load();
    value["ask_requests_total"] = ask_requests.load();
    value["semantic_cache_hits_total"] = cache_hits.load();
    value["semantic_cache_misses_total"] = cache_misses.load();
    value["rate_limited_total"] = rate_limited.load();
    value["upstream_failures_total"] = upstream_failures.load();
    value["circuit_rejections_total"] = circuit_rejections.load();
    value["rerank_failures_total"] = rerank_failures.load();
    value["active_model_streams"] = stream_active.load();
    const auto requests = recommendation_requests.load();
    value["recommendation_requests_total"] = requests;
    value["recommendation_duration_us_total"] = recommendation_duration_us.load();
    value["recommendation_duration_ms_average"] = requests == 0 ? 0.0 :
        recommendation_duration_us.load() / 1000.0 / requests;
    value["recommendation_candidates_total"] = recommendation_candidates.load();
    value["recommendation_candidates_average"] = requests == 0 ? 0.0 :
        static_cast<double>(recommendation_candidates.load()) / requests;
    value["recommendation_fallbacks_total"] = recommendation_fallbacks.load();
    value["recommendation_embedding_failures_total"] = recommendation_embedding_failures.load();
    value["recommendation_snapshot_hits_total"] = recommendation_snapshot_hits.load();
    return boost::json::serialize(value);
}
