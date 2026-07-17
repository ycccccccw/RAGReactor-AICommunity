#ifndef RAGREACTOR_RAG_SERVICE_H
#define RAGREACTOR_RAG_SERVICE_H

#include "bailian_embedding_provider.h"
#include "llm_client.h"
#include "prompt_builder.h"
#include "vector_store.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace rag
{
struct RagAnswer
{
    std::string text;
    std::vector<SearchResult> sources;
    bool used_knowledge = false;
};

struct PreparedRag
{
    std::string prompt;
    std::string fallback_answer;
    std::vector<SearchResult> sources;
    bool used_knowledge = false;
};

class RagService
{
public:
    static RagService &instance();

    bool configured() const { return configured_; }
    bool index_ready() const;
    std::string provider_name() const;
    PreparedRag prepare(const std::string &question, std::size_t top_k);
    void stream_answer(const PreparedRag &prepared,
                       const std::function<bool(const std::string &)> &on_delta,
                       const std::atomic<bool> &canceled) const;
    RagAnswer ask(const std::string &question, std::size_t top_k);

private:
    RagService();
    bool ensure_index(std::string *error);

    bool configured_;
    std::string configuration_error_;
    std::string document_directory_;
    std::string index_path_;
    float relevance_threshold_;
    std::unique_ptr<BailianEmbeddingProvider> embedding_;
    std::unique_ptr<LlmClient> llm_;
    PromptBuilder prompt_builder_;
    VectorStore store_;
    bool index_initialized_;
    mutable std::mutex index_mutex_;
};
}

#endif
