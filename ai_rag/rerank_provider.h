#ifndef RAGREACTOR_RERANK_PROVIDER_H
#define RAGREACTOR_RERANK_PROVIDER_H

#include "vector_store.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{
class RerankProvider
{
public:
    virtual ~RerankProvider() = default;
    virtual std::vector<SearchResult> rerank(const std::string &query,
                                              const std::vector<SearchResult> &candidates,
                                              std::size_t top_n) const = 0;
};
}

#endif
