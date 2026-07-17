#ifndef RAGREACTOR_BAILIAN_RERANK_PROVIDER_H
#define RAGREACTOR_BAILIAN_RERANK_PROVIDER_H

#include "http_json_client.h"
#include "rerank_provider.h"

namespace rag
{
class BailianRerankProvider : public RerankProvider
{
public:
    BailianRerankProvider(std::string compatible_base_url, std::string api_key,
                          std::string model = "qwen3-rerank",
                          long connect_timeout_ms = 3000,
                          long request_timeout_ms = 10000);
    std::vector<SearchResult> rerank(const std::string &query,
                                      const std::vector<SearchResult> &candidates,
                                      std::size_t top_n) const override;

private:
    std::string endpoint_;
    std::string api_key_;
    std::string model_;
    HttpJsonClient client_;
};
}

#endif
