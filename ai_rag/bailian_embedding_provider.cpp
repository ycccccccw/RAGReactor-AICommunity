#include "bailian_embedding_provider.h"

#include <boost/json.hpp>

#include <stdexcept>

namespace json = boost::json;

namespace rag
{
namespace
{
std::string trim_slash(std::string value)
{
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}
}

BailianEmbeddingProvider::BailianEmbeddingProvider(
    std::string base_url, std::string api_key, std::string model,
    std::size_t dimension, long connect_timeout_ms, long request_timeout_ms)
    : base_url_(trim_slash(std::move(base_url))), api_key_(std::move(api_key)),
      model_(std::move(model)), dimension_(dimension),
      client_(connect_timeout_ms, request_timeout_ms)
{
    if (base_url_.empty() || api_key_.empty() || model_.empty() || dimension_ == 0)
        throw std::invalid_argument("invalid Bailian embedding configuration");
}

std::vector<float> BailianEmbeddingProvider::embed(const std::string &text) const
{
    json::object request;
    request["model"] = model_;
    request["input"] = text;
    request["dimensions"] = dimension_;
    request["encoding_format"] = "float";

    const json::value response = client_.post(base_url_ + "/embeddings", api_key_, request);
    if (!response.is_object()) throw std::runtime_error("embedding response is not an object");
    const json::value *data = response.as_object().if_contains("data");
    if (!data || !data->is_array() || data->as_array().empty())
        throw std::runtime_error("embedding response has no data");
    const json::value &first = data->as_array().front();
    if (!first.is_object()) throw std::runtime_error("invalid embedding data item");
    const json::value *embedding = first.as_object().if_contains("embedding");
    if (!embedding || !embedding->is_array())
        throw std::runtime_error("embedding response has no vector");

    std::vector<float> vector;
    vector.reserve(embedding->as_array().size());
    for (const json::value &number : embedding->as_array())
    {
        if (number.is_double()) vector.push_back(static_cast<float>(number.as_double()));
        else if (number.is_int64()) vector.push_back(static_cast<float>(number.as_int64()));
        else if (number.is_uint64()) vector.push_back(static_cast<float>(number.as_uint64()));
        else throw std::runtime_error("embedding vector contains a non-number");
    }
    if (vector.size() != dimension_)
        throw std::runtime_error("unexpected embedding dimension: " +
                                 std::to_string(vector.size()));
    return vector;
}
}
