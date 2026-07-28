#include "bailian_rerank_provider.h"

#include <boost/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace json = boost::json;

namespace rag
{
namespace
{
std::string rerank_endpoint(std::string endpoint)
{
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    if (endpoint.empty()) throw std::invalid_argument("RAG_RERANK_URL is empty");
    return endpoint;
}
}

BailianRerankProvider::BailianRerankProvider(
    std::string endpoint, std::string api_key, std::string model,
    long connect_timeout_ms, long request_timeout_ms)
    : endpoint_(rerank_endpoint(std::move(endpoint))),
      api_key_(std::move(api_key)), model_(std::move(model)),
      client_(connect_timeout_ms, request_timeout_ms)
{
    if (api_key_.empty() || model_.empty())
        throw std::invalid_argument("invalid Bailian rerank configuration");
}

std::vector<SearchResult> BailianRerankProvider::rerank(
    const std::string &query, const std::vector<SearchResult> &candidates,
    std::size_t top_n) const
{
    if (candidates.empty() || top_n == 0) return {};
    json::array documents;
    for (const SearchResult &candidate : candidates)
        documents.push_back(json::value(candidate.chunk.text));

    json::object input;
    input["query"] = query;
    input["documents"] = std::move(documents);
    json::object parameters;
    parameters["top_n"] = std::min(top_n, candidates.size());
    parameters["return_documents"] = false;
    json::object request;
    request["model"] = model_;
    request["input"] = std::move(input);
    request["parameters"] = std::move(parameters);

    const json::value response = client_.post(endpoint_, api_key_, request);
    if (!response.is_object()) throw std::runtime_error("rerank response is not an object");
    const json::value *output = response.as_object().if_contains("output");
    if (!output || !output->is_object())
        throw std::runtime_error("rerank response has no output");
    const json::value *results = output->as_object().if_contains("results");
    if (!results || !results->is_array())
        throw std::runtime_error("rerank response has no results");

    std::vector<SearchResult> ranked;
    ranked.reserve(results->as_array().size());
    for (const json::value &item : results->as_array())
    {
        if (!item.is_object()) continue;
        const json::value *index = item.as_object().if_contains("index");
        const json::value *score = item.as_object().if_contains("relevance_score");
        if (!index || !score) continue;
        std::size_t candidate_index;
        if (index->is_int64()) candidate_index = static_cast<std::size_t>(index->as_int64());
        else if (index->is_uint64()) candidate_index = static_cast<std::size_t>(index->as_uint64());
        else continue;
        if (candidate_index >= candidates.size()) continue;
        SearchResult result = candidates[candidate_index];
        if (score->is_double()) result.score = static_cast<float>(score->as_double());
        else if (score->is_int64()) result.score = static_cast<float>(score->as_int64());
        else if (score->is_uint64()) result.score = static_cast<float>(score->as_uint64());
        else continue;
        ranked.push_back(std::move(result));
    }
    if (ranked.empty()) throw std::runtime_error("rerank returned no valid candidates");
    return ranked;
}
}
