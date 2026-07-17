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
    return boost::json::serialize(value);
}
