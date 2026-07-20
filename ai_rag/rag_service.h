#ifndef RAGREACTOR_RAG_SERVICE_H
#define RAGREACTOR_RAG_SERVICE_H

#include "bailian_embedding_provider.h"
#include "llm_client.h"
#include "prompt_builder.h"
#include "resilience.h"
#include "hybrid_retriever.h"
#include "bailian_rerank_provider.h"
#include "vector_store.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <stdexcept>

namespace rag
{
struct RagAnswer
{
    std::string text;
    std::vector<SearchResult> sources;
    bool used_knowledge = false;
    bool cache_hit = false;
    bool rerank_applied = false;
    bool rerank_fallback = false;
};

struct PreparedRag
{
    std::vector<float> query_embedding;
    std::string prompt;
    std::string fallback_answer;
    std::string cached_answer;
    std::vector<SearchResult> sources;
    bool used_knowledge = false;
    bool cache_hit = false;
    bool rerank_applied = false;
    bool rerank_fallback = false;
    bool cacheable = true;
};

struct RagQueryOptions
{
    bool include_community = false;
    std::string username;
};

class CircuitOpenError : public std::runtime_error
{
public:
    CircuitOpenError() : std::runtime_error("model circuit breaker is open; retry later") {}
};

class RagService
{
public:
    static RagService &instance();

    bool configured() const { return configured_; }
    bool index_ready() const;
    std::string provider_name() const;
    PreparedRag prepare(const std::string &question, std::size_t top_k,
                        const RagQueryOptions &options = {});
    void stream_answer(const PreparedRag &prepared,
                       const std::function<bool(const std::string &)> &on_delta,
                       const std::atomic<bool> &canceled);
    RagAnswer ask(const std::string &question, std::size_t top_k,
                  const RagQueryOptions &options = {});
    std::vector<SearchResult> retrieve(const std::string &query, std::size_t top_k,
                                       bool include_knowledge, bool include_community,
                                       const std::string &exclude_community_id = "");
    std::size_t cache_size() const { return cache_.size(); }
    bool circuit_open() const { return circuit_.open(); }
    std::string retrieval_mode() const;
    bool rerank_enabled() const { return reranker_ != nullptr; }

private:
    RagService();
    bool ensure_index(std::string *error);

    bool configured_;
    std::string configuration_error_;
    std::string document_directory_;
    std::string index_path_;
    std::string hnsw_index_path_;
    std::string community_index_path_;
    float relevance_threshold_;
    std::size_t retrieval_candidates_;
    std::size_t rerank_top_n_;
    std::unique_ptr<BailianEmbeddingProvider> embedding_;
    std::unique_ptr<LlmClient> llm_;
    PromptBuilder prompt_builder_;
    SemanticCache cache_;
    CircuitBreaker circuit_;
    VectorStore store_;
    std::unique_ptr<HnswIndex> hnsw_;
    Bm25Index bm25_;
    std::unique_ptr<BailianRerankProvider> reranker_;
    bool index_initialized_;
    mutable std::mutex index_mutex_;
};
}

#endif
