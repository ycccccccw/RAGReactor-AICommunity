#include "hybrid_retriever.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <unordered_set>

namespace rag
{
namespace
{
std::vector<std::string> utf8_units(const std::string &text)
{
    std::vector<std::string> units;
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char first = static_cast<unsigned char>(text[i]);
        std::size_t length = first < 0x80 ? 1 : (first < 0xE0 ? 2 : (first < 0xF0 ? 3 : 4));
        if (i + length > text.size()) length = 1;
        units.push_back(text.substr(i, length));
        i += length;
    }
    return units;
}

std::string chunk_key(const DocumentChunk &chunk)
{
    return chunk.source + "#" + std::to_string(chunk.chunk_index);
}
}

std::vector<std::string> Bm25Index::tokenize(const std::string &text)
{
    std::vector<std::string> tokens;
    std::string ascii;
    std::vector<std::string> chinese;
    auto flush_ascii = [&]() {
        if (!ascii.empty()) { tokens.push_back(ascii); ascii.clear(); }
    };
    for (const std::string &unit : utf8_units(text))
    {
        const unsigned char first = static_cast<unsigned char>(unit[0]);
        if (unit.size() == 1 && (std::isalnum(first) || first == '_'))
        {
            ascii.push_back(static_cast<char>(std::tolower(first)));
            continue;
        }
        flush_ascii();
        if (unit.size() > 1)
        {
            tokens.push_back(unit);
            chinese.push_back(unit);
        }
        else
            chinese.clear();
        if (chinese.size() >= 2)
            tokens.push_back(chinese[chinese.size() - 2] + chinese.back());
    }
    flush_ascii();
    return tokens;
}

void Bm25Index::build(const VectorStore &store)
{
    postings_.clear();
    document_lengths_.clear();
    store_ = &store;
    std::size_t total_length = 0;
    for (std::size_t document = 0; document < store.chunks().size(); ++document)
    {
        const std::vector<std::string> tokens = tokenize(store.chunks()[document].text);
        document_lengths_.push_back(tokens.size());
        total_length += tokens.size();
        std::unordered_map<std::string, unsigned int> frequencies;
        for (const std::string &token : tokens) ++frequencies[token];
        for (const auto &item : frequencies)
            postings_[item.first].push_back({document, item.second});
    }
    average_length_ = store.size() == 0 ? 0.0 :
        static_cast<double>(total_length) / store.size();
}

std::vector<SearchResult> Bm25Index::search(const std::string &query,
                                            std::size_t top_k) const
{
    if (!store_ || store_->size() == 0 || top_k == 0 || average_length_ == 0.0) return {};
    std::unordered_set<std::string> unique_terms;
    for (const std::string &token : tokenize(query)) unique_terms.insert(token);
    std::unordered_map<std::size_t, double> scores;
    const double document_count = static_cast<double>(store_->size());
    constexpr double k1 = 1.5;
    constexpr double b = 0.75;
    for (const std::string &term : unique_terms)
    {
        const auto found = postings_.find(term);
        if (found == postings_.end()) continue;
        const double df = static_cast<double>(found->second.size());
        const double idf = std::log(1.0 + (document_count - df + 0.5) / (df + 0.5));
        for (const auto &posting : found->second)
        {
            const double tf = posting.second;
            const double length = document_lengths_[posting.first];
            scores[posting.first] += idf * tf * (k1 + 1.0) /
                (tf + k1 * (1.0 - b + b * length / average_length_));
        }
    }
    std::vector<std::pair<std::size_t, double>> ranked(scores.begin(), scores.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        return left.second > right.second;
    });
    if (ranked.size() > top_k) ranked.resize(top_k);
    std::vector<SearchResult> results;
    for (const auto &item : ranked)
        results.push_back({store_->chunks()[item.first], static_cast<float>(item.second)});
    return results;
}

HybridRetriever::HybridRetriever(const VectorStore &store, HnswIndex *hnsw,
                                 const Bm25Index *bm25, std::size_t candidate_count,
                                 float rrf_k)
    : store_(store), hnsw_(hnsw), bm25_(bm25), candidate_count_(candidate_count),
      rrf_k_(rrf_k) {}

std::vector<SearchResult> HybridRetriever::search(
    const std::string &query_text, const std::vector<float> &query_vector,
    std::size_t final_count) const
{
    std::vector<SearchResult> vector_results = hnsw_ && hnsw_->ready()
        ? hnsw_->search(query_vector, candidate_count_)
        : store_.search(query_vector, candidate_count_);
    const std::vector<SearchResult> keyword_results = bm25_ && bm25_->ready()
        ? bm25_->search(query_text, candidate_count_) : std::vector<SearchResult>{};

    struct Fused { SearchResult result; float score = 0.0f; };
    std::map<std::string, Fused> fused;
    const auto add_ranked = [&](const std::vector<SearchResult> &results) {
        for (std::size_t rank = 0; rank < results.size(); ++rank)
        {
            const std::string key = chunk_key(results[rank].chunk);
            Fused &entry = fused[key];
            if (entry.result.chunk.text.empty()) entry.result = results[rank];
            entry.score += 1.0f / (rrf_k_ + static_cast<float>(rank + 1));
        }
    };
    add_ranked(vector_results);
    add_ranked(keyword_results);

    std::vector<SearchResult> results;
    for (auto &item : fused)
    {
        item.second.result.score = item.second.score;
        results.push_back(std::move(item.second.result));
    }
    std::sort(results.begin(), results.end(), [](const SearchResult &left,
                                                  const SearchResult &right) {
        return left.score > right.score;
    });
    if (results.size() > final_count) results.resize(final_count);
    return results;
}
}
