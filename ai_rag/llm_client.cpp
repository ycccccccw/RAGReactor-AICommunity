#include "llm_client.h"

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

LlmClient::LlmClient(std::string base_url, std::string api_key,
                     std::string model, std::size_t max_output_tokens)
    : base_url_(trim_slash(std::move(base_url))), api_key_(std::move(api_key)),
      model_(std::move(model)), max_output_tokens_(max_output_tokens),
      client_(3000, 60000)
{
    if (base_url_.empty() || api_key_.empty() || model_.empty())
        throw std::invalid_argument("invalid Bailian LLM configuration");
}

std::string LlmClient::answer(const std::string &prompt) const
{
    json::object system_message;
    system_message["role"] = "system";
    system_message["content"] =
        "你是本地知识库助手。只能根据用户消息中的知识库片段回答，不得编造。";
    json::object user_message;
    user_message["role"] = "user";
    user_message["content"] = prompt;

    json::array messages;
    messages.push_back(std::move(system_message));
    messages.push_back(std::move(user_message));
    json::object request;
    request["model"] = model_;
    request["messages"] = std::move(messages);
    request["temperature"] = 0.2;
    request["max_tokens"] = max_output_tokens_;
    request["stream"] = false;

    const json::value response = client_.post(base_url_ + "/chat/completions",
                                               api_key_, request);
    if (!response.is_object()) throw std::runtime_error("LLM response is not an object");
    const json::value *choices = response.as_object().if_contains("choices");
    if (!choices || !choices->is_array() || choices->as_array().empty())
        throw std::runtime_error("LLM response has no choices");
    const json::value &choice = choices->as_array().front();
    if (!choice.is_object()) throw std::runtime_error("invalid LLM choice");
    const json::value *message = choice.as_object().if_contains("message");
    if (!message || !message->is_object()) throw std::runtime_error("LLM response has no message");
    const json::value *content = message->as_object().if_contains("content");
    if (!content || !content->is_string()) throw std::runtime_error("LLM response has no text");
    return std::string(content->as_string());
}
}
