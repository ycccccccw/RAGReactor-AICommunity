#include "hnsw_index.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace rag
{
HnswIndex::HnswIndex(std::size_t dimension, std::size_t m,
                     std::size_t ef_construction, std::size_t ef_search)
    : dimension_(dimension), m_(m), ef_construction_(ef_construction),
      ef_search_(ef_search),
      space_(std::make_unique<hnswlib::InnerProductSpace>(dimension)) {}

std::vector<float> HnswIndex::normalize(const std::vector<float> &vector)
{
    double norm = 0.0;
    for (float value : vector) norm += static_cast<double>(value) * value;
    if (norm == 0.0) return vector;
    const float divisor = static_cast<float>(std::sqrt(norm));
    std::vector<float> normalized = vector;
    for (float &value : normalized) value /= divisor;
    return normalized;
}

bool HnswIndex::build(const VectorStore &store, std::string *error)
{
    if (store.dimension() != dimension_ || store.size() == 0)
    {
        if (error) *error = "HNSW store is empty or has an unexpected dimension";
        return false;
    }
    try
    {
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            space_.get(), store.size(), m_, ef_construction_);
        const auto &chunks = store.chunks();
        for (std::size_t i = 0; i < chunks.size(); ++i)
        {
            std::vector<float> vector = normalize(chunks[i].embedding);
            index_->addPoint(vector.data(), i);
        }
        index_->setEf(std::max(ef_search_, static_cast<std::size_t>(10)));
        store_ = &store;
        return true;
    }
    catch (const std::exception &exception)
    {
        index_.reset();
        if (error) *error = exception.what();
        return false;
    }
}

bool HnswIndex::save(const std::string &path, std::string *error) const
{
    if (!index_)
    {
        if (error) *error = "HNSW index is not built";
        return false;
    }
    try { index_->saveIndex(path); return true; }
    catch (const std::exception &exception)
    {
        if (error) *error = exception.what();
        return false;
    }
}

bool HnswIndex::load(const std::string &path, const VectorStore &store,
                     std::string *error)
{
    if (store.dimension() != dimension_ || store.size() == 0)
    {
        if (error) *error = "HNSW metadata does not match vector store";
        return false;
    }
    try
    {
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(space_.get(), path);
        if (index_->getCurrentElementCount() != store.size())
            throw std::runtime_error("HNSW element count does not match vector store");
        index_->setEf(std::max(ef_search_, static_cast<std::size_t>(10)));
        store_ = &store;
        return true;
    }
    catch (const std::exception &exception)
    {
        index_.reset();
        if (error) *error = exception.what();
        return false;
    }
}

std::vector<SearchResult> HnswIndex::search(const std::vector<float> &query,
                                            std::size_t top_k,
                                            const ContentFilter &filter) const
{
    if (!ready() || query.size() != dimension_ || top_k == 0) return {};
    std::vector<float> normalized = normalize(query);
    const std::size_t count = store_->size();
    auto queue = index_->searchKnn(normalized.data(), count);
    std::vector<SearchResult> candidates;
    candidates.reserve(queue.size());
    while (!queue.empty())
    {
        const auto item = queue.top();
        queue.pop();
        if (item.second >= store_->chunks().size()) continue;
        const DocumentChunk &chunk = store_->chunks()[item.second];
        if (!filter.matches(chunk)) continue;
        candidates.push_back({chunk, 1.0f - item.first});
    }
    std::sort(candidates.begin(), candidates.end(), [](const SearchResult &left,
                                                       const SearchResult &right) {
        return left.score > right.score;
    });
    if (candidates.size() > top_k) candidates.resize(top_k);
    return candidates;
}
}
