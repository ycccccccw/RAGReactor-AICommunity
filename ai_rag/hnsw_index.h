#ifndef RAGREACTOR_HNSW_INDEX_H
#define RAGREACTOR_HNSW_INDEX_H

#include "vector_store.h"

#include <hnswlib/hnswlib.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rag
{
class HnswIndex
{
public:
    HnswIndex(std::size_t dimension, std::size_t m = 16,
              std::size_t ef_construction = 200, std::size_t ef_search = 50);
    bool build(const VectorStore &store, std::string *error = nullptr);
    bool save(const std::string &path, std::string *error = nullptr) const;
    bool load(const std::string &path, const VectorStore &store,
              std::string *error = nullptr);
    std::vector<SearchResult> search(const std::vector<float> &query,
                                     std::size_t top_k) const;
    bool ready() const { return index_ != nullptr && store_ != nullptr; }

private:
    static std::vector<float> normalize(const std::vector<float> &vector);
    std::size_t dimension_;
    std::size_t m_;
    std::size_t ef_construction_;
    std::size_t ef_search_;
    std::unique_ptr<hnswlib::InnerProductSpace> space_;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index_;
    const VectorStore *store_ = nullptr;
};
}

#endif
