#include "rag_service.h"

#include "knowledge_indexer.h"
#include "text_splitter.h"
#include "../community/question_interest_store.h"

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

bool environment_bool(const char *name, bool fallback)
{
    const std::string value = environment(name);
    if (value.empty()) return fallback;
    return value == "1" || value == "true" || value == "TRUE" || value == "yes";
}

unsigned int environment_uint(const char *name, unsigned int fallback)
{
    const std::string value = environment(name);
    if (value.empty()) return fallback;
    try { return static_cast<unsigned int>(std::stoul(value)); }
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
      hnsw_index_path_(environment("RAG_HNSW_INDEX_PATH",
                                   "knowledge/index/bailian-v4-1024.hnsw")),
      community_index_path_(environment("COMMUNITY_INDEX_PATH",
                                        "knowledge/index/community-bailian-v4-1024.ragvec")),
      relevance_threshold_(environment_float("RAG_RELEVANCE_THRESHOLD", 0.45f)),
      retrieval_candidates_(environment_size("RAG_RERANK_CANDIDATES", 20)),
      rerank_top_n_(environment_size("RAG_RERANK_TOP_N", 5)),
      prompt_builder_(environment_size("RAG_MAX_PROMPT_BYTES", 12000)),
      cache_(environment_size("RAG_CACHE_CAPACITY", 100),
             environment_float("RAG_CACHE_SIMILARITY", 0.95f),
             std::chrono::seconds(environment_size("RAG_CACHE_TTL_SECONDS", 600))),
      circuit_(environment_uint("RAG_CIRCUIT_FAILURE_THRESHOLD", 3),
               std::chrono::seconds(environment_size("RAG_CIRCUIT_COOLDOWN_SECONDS", 30))),
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
            environment_size("RAG_EMBEDDING_DIMENSION", 1024),
            environment_size("RAG_CONNECT_TIMEOUT_MS", 5000),
            environment_size("RAG_EMBEDDING_TIMEOUT_MS", 10000));
        llm_ = std::make_unique<LlmClient>(
            base_url, key, llm_model,
            environment_size("RAG_MAX_OUTPUT_TOKENS", 800),
            environment_size("RAG_CONNECT_TIMEOUT_MS", 5000),
            environment_size("RAG_LLM_TIMEOUT_MS", 30000));
        if (environment_bool("RAG_RERANK_ENABLED", true))
            reranker_ = std::make_unique<BailianRerankProvider>(
                base_url, key, environment("RAG_RERANK_MODEL", "qwen3-rerank"),
                environment_size("RAG_CONNECT_TIMEOUT_MS", 5000),
                environment_size("RAG_RERANK_TIMEOUT_MS", 10000));
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

    bool vector_store_loaded = false;
    if (fs::exists(index_path_))
    {
        vector_store_loaded = store_.load(index_path_, error) &&
                              store_.dimension() == embedding_->dimension();
        if (!vector_store_loaded) store_.clear();
    }

    if (!vector_store_loaded)
    {
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
        }
        catch (const std::exception &exception)
        {
            if (error) *error = exception.what();
            return false;
        }
    }

    try
    {
        hnsw_ = std::make_unique<HnswIndex>(
            store_.dimension(), environment_size("RAG_HNSW_M", 16),
            environment_size("RAG_HNSW_EF_CONSTRUCTION", 200),
            environment_size("RAG_HNSW_EF_SEARCH", 50));
        std::string hnsw_error;
        if (!(fs::exists(hnsw_index_path_) &&
              hnsw_->load(hnsw_index_path_, store_, &hnsw_error)))
        {
            if (!hnsw_->build(store_, &hnsw_error)) hnsw_.reset();
            else hnsw_->save(hnsw_index_path_, nullptr);
        }
        bm25_.build(store_);
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

std::string RagService::retrieval_mode() const
{
    return reranker_ ? "hnsw+bm25+rrf+qwen3-rerank" : "hnsw+bm25+rrf";
}

std::vector<SearchResult> RagService::retrieve(const std::string &query, std::size_t top_k,
                                               bool include_knowledge, bool include_community,
                                               const std::string &exclude_community_id)
{
    std::string error;
    if (!ensure_index(&error)) throw std::runtime_error("knowledge index unavailable: " + error);
    if (!circuit_.allow_request()) throw CircuitOpenError();
    std::vector<float> embedding;
    try
    {
        embedding = embedding_->embed(query);
    }
    catch (...)
    {
        circuit_.record_failure();
        throw;
    }

    std::vector<SearchResult> results;
    if (include_knowledge)
    {
        HybridRetriever retriever(store_, hnsw_.get(), &bm25_, retrieval_candidates_);
        ContentFilter filter;
        filter.source_types = {"knowledge"};
        results = retriever.search(query, embedding, retrieval_candidates_, filter);
    }
    if (include_community && fs::exists(community_index_path_))
    {
        VectorStore community;
        if (community.load(community_index_path_, &error) && community.dimension() == embedding.size())
        {
            Bm25Index community_bm25;
            community_bm25.build(community);
            HybridRetriever retriever(community, nullptr, &community_bm25, retrieval_candidates_);
            ContentFilter filter;
            filter.source_types = {"community"};
            auto community_results = retriever.search(
                query, embedding, retrieval_candidates_, filter);
            community_results.erase(std::remove_if(community_results.begin(), community_results.end(),
                [&](const SearchResult &value) {
                    return !exclude_community_id.empty() &&
                           value.chunk.source_id == exclude_community_id;
                }), community_results.end());
            results.insert(results.end(), community_results.begin(), community_results.end());
        }
    }
    std::sort(results.begin(), results.end(), [](const SearchResult &left, const SearchResult &right) {
        return left.score > right.score;
    });
    if (reranker_ && !results.empty())
    {
        try { results = reranker_->rerank(query, results, top_k); }
        catch (...) { if (results.size() > top_k) results.resize(top_k); }
    }
    else if (results.size() > top_k) results.resize(top_k);
    circuit_.record_success();
    return results;
}

PreparedRag RagService::prepare(const std::string &question, std::size_t top_k,
                                const RagQueryOptions &options)
{
    std::string error;
    if (!ensure_index(&error)) throw std::runtime_error("knowledge index unavailable: " + error);
    if (!circuit_.allow_request()) throw CircuitOpenError();

    PreparedRag prepared;
    prepared.cacheable = !options.include_community;
    try { prepared.query_embedding = embedding_->embed(question); }
    catch (...) { circuit_.record_failure(); throw; }

    if (!options.username.empty())
        QuestionInterestStore::instance().remember(options.username, prepared.query_embedding);

    if (!options.include_community)
    if (const auto cached = cache_.lookup(prepared.query_embedding))
    {
        prepared.cached_answer = cached->answer;
        prepared.sources = cached->sources;
        prepared.used_knowledge = cached->used_knowledge;
        prepared.cache_hit = true;
        return prepared;
    }

    HybridRetriever retriever(store_, hnsw_.get(), &bm25_, retrieval_candidates_);
    ContentFilter knowledge_only;
    knowledge_only.source_types = {"knowledge"};
    std::vector<SearchResult> results = retriever.search(
        question, prepared.query_embedding, retrieval_candidates_, knowledge_only);
    if (options.include_community && fs::exists(community_index_path_))
    {
        VectorStore community;
        if (community.load(community_index_path_, &error) &&
            community.dimension() == prepared.query_embedding.size())
        {
            Bm25Index community_bm25;
            community_bm25.build(community);
            HybridRetriever community_retriever(
                community, nullptr, &community_bm25, retrieval_candidates_);
            ContentFilter community_only;
            community_only.source_types = {"community"};
            auto extra = community_retriever.search(
                question, prepared.query_embedding, retrieval_candidates_, community_only);
            results.insert(results.end(), extra.begin(), extra.end());
            std::sort(results.begin(), results.end(), [](const SearchResult &left,
                                                         const SearchResult &right) {
                return left.score > right.score;
            });
        }
    }
    bool reranked = false;
    if (reranker_ && !results.empty())
    {
        try
        {
            results = reranker_->rerank(question, results,
                std::min(top_k, rerank_top_n_));
            reranked = true;
            prepared.rerank_applied = true;
        }
        catch (...)
        {
            // Rerank is an optional precision layer. Its failure must not make the
            // entire RAG service unavailable; keep the local RRF ordering.
            prepared.rerank_fallback = true;
        }
    }
    if (!reranked && results.size() > top_k) results.resize(top_k);
    if (reranked)
        results.erase(std::remove_if(results.begin(), results.end(), [this](const SearchResult &result) {
            return result.score < relevance_threshold_;
        }), results.end());
    if (results.empty())
    {
        prepared.fallback_answer =
            "本地知识库中没有找到与该问题足够相关的内容，因此我无法基于知识库回答。";
        circuit_.record_success();
        if (!options.include_community)
            cache_.put(prepared.query_embedding, {prepared.fallback_answer, {}, false});
        return prepared;
    }

    prepared.sources = std::move(results);
    prepared.prompt = prompt_builder_.build(question, prepared.sources);
    prepared.used_knowledge = true;
    return prepared;
}

void RagService::stream_answer(
    const PreparedRag &prepared,
    const std::function<bool(const std::string &)> &on_delta,
    const std::atomic<bool> &canceled)
{
    if (prepared.cache_hit)
    {
        if (!canceled.load()) on_delta(prepared.cached_answer);
        return;
    }
    if (!prepared.used_knowledge)
    {
        if (!canceled.load()) on_delta(prepared.fallback_answer);
        return;
    }
    std::string complete_answer;
    bool consumer_ready = true;
    try
    {
        llm_->stream_answer(prepared.prompt, [&](const std::string &delta) {
            complete_answer += delta;
            consumer_ready = on_delta(delta);
            return consumer_ready;
        }, canceled);
        if (!canceled.load() && consumer_ready && !complete_answer.empty())
        {
            circuit_.record_success();
            if (prepared.cacheable)
                cache_.put(prepared.query_embedding,
                           {complete_answer, prepared.sources, prepared.used_knowledge});
        }
    }
    catch (...)
    {
        circuit_.record_failure();
        throw;
    }
}

RagAnswer RagService::ask(const std::string &question, std::size_t top_k,
                          const RagQueryOptions &options)
{
    PreparedRag prepared = prepare(question, top_k, options);
    RagAnswer answer;
    answer.sources = prepared.sources;
    answer.used_knowledge = prepared.used_knowledge;
    answer.cache_hit = prepared.cache_hit;
    answer.rerank_applied = prepared.rerank_applied;
    answer.rerank_fallback = prepared.rerank_fallback;
    if (prepared.cache_hit)
        answer.text = prepared.cached_answer;
    else if (!prepared.used_knowledge)
        answer.text = prepared.fallback_answer;
    else
    {
        try
        {
            answer.text = llm_->answer(prepared.prompt);
            circuit_.record_success();
            if (prepared.cacheable)
                cache_.put(prepared.query_embedding,
                           {answer.text, answer.sources, answer.used_knowledge});
        }
        catch (...)
        {
            circuit_.record_failure();
            throw;
        }
    }
    return answer;
}
}
