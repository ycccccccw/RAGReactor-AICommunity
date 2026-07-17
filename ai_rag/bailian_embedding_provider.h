#ifndef RAGREACTOR_BAILIAN_EMBEDDING_PROVIDER_H
#define RAGREACTOR_BAILIAN_EMBEDDING_PROVIDER_H

#include "embedding_provider.h"
#include "http_json_client.h"

namespace rag
{
class BailianEmbeddingProvider : public EmbeddingProvider
{
public:
    BailianEmbeddingProvider(std::string base_url, std::string api_key,
                             std::string model, std::size_t dimension = 1024);

    std::vector<float> embed(const std::string &text) const override;
    std::size_t dimension() const override { return dimension_; }
    std::string name() const override { return model_; }

private:
    std::string base_url_;
    std::string api_key_;
    std::string model_;
    std::size_t dimension_;
    HttpJsonClient client_;
};
}

#endif
