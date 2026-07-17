#ifndef RAGREACTOR_LLM_CLIENT_H
#define RAGREACTOR_LLM_CLIENT_H

#include "http_json_client.h"

#include <string>

namespace rag
{
class LlmClient
{
public:
    LlmClient(std::string base_url, std::string api_key, std::string model,
              std::size_t max_output_tokens = 800);

    std::string answer(const std::string &prompt) const;
    const std::string &model() const { return model_; }

private:
    std::string base_url_;
    std::string api_key_;
    std::string model_;
    std::size_t max_output_tokens_;
    HttpJsonClient client_;
};
}

#endif
