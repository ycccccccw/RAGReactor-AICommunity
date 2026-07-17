#ifndef RAGREACTOR_EMBEDDING_PROVIDER_H
#define RAGREACTOR_EMBEDDING_PROVIDER_H

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{
class EmbeddingProvider
{
public:
    virtual ~EmbeddingProvider() = default;
    virtual std::vector<float> embed(const std::string &text) const = 0;
    virtual std::size_t dimension() const = 0;
    virtual std::string name() const = 0;
};

class MockEmbeddingProvider : public EmbeddingProvider
{
public:
    explicit MockEmbeddingProvider(std::size_t dimension = 256);

    std::vector<float> embed(const std::string &text) const override;
    std::size_t dimension() const override { return dimension_; }
    std::string name() const override { return "mock-hash-v1"; }

private:
    std::size_t dimension_;
};
}

#endif
