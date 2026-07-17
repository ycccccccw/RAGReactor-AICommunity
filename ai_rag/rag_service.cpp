#include "rag_service.h"

#include "knowledge_indexer.h"
#include "text_splitter.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace rag
{
namespace
{
std::string environment(const char *name, const std::string &fallback = "")
{
    const char *value = std::getenv(name);
    return value && value[0] ? std::string(value) : fallback;
}

std::size_t environment_size(const char *name, std::size_t fallback)
{
    const std::string value = environment(name);
    if (value.empty()) return fallback;
    try { return static_cast<std::size_t>(std::stoull(value)); }
    catch (...) { return fallback; }
}

float environment_float(const char *name, float fallback)
{
    const std::string value = environment(name);
    if (value.empty()) return fallback;
    try { return std::stof(value); }
    catch (...) { return fallback; }
}
}

RagService &RagService::instance()
{
    // The service intentionally lives until process termination. Detached model-stream
    // workers may still be unwinding while main() performs normal static destruction.
    static RagService *service = new RagService();
    return *service;
}

RagService::RagService()
    : configured_(false), document_directory_(environment("RAG_DOCUMENT_DIR", "knowledge/documents")),
      index_path_(environment("RAG_INDEX_PATH", "knowledge/index/bailian-v4-1024.ragvec")),
      relevance_threshold_(environment_float("RAG_RELEVANCE_THRESHOLD", 0.40f)),
      prompt_builder_(environment_size("RAG_MAX_PROMPT_BYTES", 12000)),
      index_initialized_(false)
{
    const std::string key = environment("BAILIAN_API_KEY");
    const std::string base_url = environment("BAILIAN_BASE_URL");
    const std::string embedding_model = environment("RAG_EMBEDDING_MODEL", "text-embedding-v4");
    const std::string llm_model = environment("RAG_LLM_MODEL", "qwen-plus");
    if (key.empty() || key == "PASTE_YOUR_BAILIAN_API_KEY_HERE")
    {
        configuration_error_ = "BAILIAN_API_KEY is not configured";
        return;
    }
    if (base_url.empty())
    {
        configuration_error_ = "BAILIAN_BASE_URL is not configured";
        return;
    }

    try
    {
        embedding_ = std::make_unique<BailianEmbeddingProvider>(
            base_url, key, embedding_model,
            environment_size("RAG_EMBEDDING_DIMENSION", 1024));
        llm_ = std::make_unique<LlmClient>(
            base_url, key, llm_model,
            environment_size("RAG_MAX_OUTPUT_TOKENS", 800));
        configured_ = true;
    }
    catch (const std::exception &exception)
    {
        configuration_error_ = exception.what();
    }
}

bool RagService::ensure_index(std::string *error)
{
    std::lock_guard<std::mutex> guard(index_mutex_);
    if (index_initialized_) return true;
    if (!configured_)
    {
        if (error) *error = configuration_error_;
        return false;
    }

    if (fs::exists(index_path_))
    {
        if (store_.load(index_path_, error) &&
            store_.dimension() == embedding_->dimension())
        {
            index_initialized_ = true;
            return true;
        }
        store_.clear();
    }

    try
    {
        KnowledgeIndexer indexer(*embedding_, TextSplitter(500, 80));
        IndexBuildStats stats;
        if (!indexer.build(document_directory_, store_, stats, error)) return false;
        if (store_.size() == 0)
        {
            if (error) *error = "knowledge directory contains no supported documents";
            return false;
        }
        if (!store_.save(index_path_, error)) return false;
        index_initialized_ = true;
        return true;
    }
    catch (const std::exception &exception)
    {
        if (error) *error = exception.what();
        return false;
    }
}

bool RagService::index_ready() const
{
    std::lock_guard<std::mutex> guard(index_mutex_);
    return index_initialized_ || fs::exists(index_path_);
}

std::string RagService::provider_name() const
{
    return configured_ ? "aliyun-bailian" : "not-configured";
}

PreparedRag RagService::prepare(const std::string &question, std::size_t top_k)
{
    std::string error;
    if (!ensure_index(&error)) throw std::runtime_error("knowledge index unavailable: " + error);

    const std::vector<float> query = embedding_->embed(question);
    std::vector<SearchResult> results = store_.search(query, top_k);
    results.erase(std::remove_if(results.begin(), results.end(), [this](const SearchResult &result) {
        return result.score < relevance_threshold_;
    }), results.end());
    if (results.empty())
    {
        PreparedRag prepared;
        prepared.fallback_answer =
            "本地知识库中没有找到与该问题足够相关的内容，因此我无法基于知识库回答。";
        return prepared;
    }

    PreparedRag prepared;
    prepared.sources = std::move(results);
    prepared.prompt = prompt_builder_.build(question, prepared.sources);
    prepared.used_knowledge = true;
    return prepared;
}

void RagService::stream_answer(
    const PreparedRag &prepared,
    const std::function<bool(const std::string &)> &on_delta,
    const std::atomic<bool> &canceled) const
{
    if (!prepared.used_knowledge)
    {
        if (!canceled.load()) on_delta(prepared.fallback_answer);
        return;
    }
    llm_->stream_answer(prepared.prompt, on_delta, canceled);
}

RagAnswer RagService::ask(const std::string &question, std::size_t top_k)
{
    PreparedRag prepared = prepare(question, top_k);
    RagAnswer answer;
    answer.sources = prepared.sources;
    answer.used_knowledge = prepared.used_knowledge;
    answer.text = prepared.used_knowledge ? llm_->answer(prepared.prompt)
                                         : prepared.fallback_answer;
    return answer;
}
}
