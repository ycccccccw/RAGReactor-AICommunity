#ifndef RAGREACTOR_HYBRID_RETRIEVER_H
#define RAGREACTOR_HYBRID_RETRIEVER_H

#include "hnsw_index.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rag
{
class Bm25Index
{
public:
    void build(const VectorStore &store);
    std::vector<SearchResult> search(const std::string &query, std::size_t top_k,
                                     const ContentFilter &filter = ContentFilter()) const;
    bool ready() const { return store_ != nullptr; }
    static std::vector<std::string> tokenize(const std::string &text);

private:
    const VectorStore *store_ = nullptr;
    std::unordered_map<std::string, std::vector<std::pair<std::size_t, unsigned int>>> postings_;
    std::vector<std::size_t> document_lengths_;
    double average_length_ = 0.0;
};

class HybridRetriever
{
public:
    HybridRetriever(const VectorStore &store, HnswIndex *hnsw, const Bm25Index *bm25,
                    std::size_t candidate_count = 20, float rrf_k = 60.0f);
    std::vector<SearchResult> search(const std::string &query_text,
                                     const std::vector<float> &query_vector,
                                     std::size_t final_count,
                                     const ContentFilter &filter = ContentFilter()) const;

private:
    const VectorStore &store_;
    HnswIndex *hnsw_;
    const Bm25Index *bm25_;
    std::size_t candidate_count_;
    float rrf_k_;
};
}

#endif
