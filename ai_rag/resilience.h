#ifndef RAGREACTOR_RESILIENCE_H
#define RAGREACTOR_RESILIENCE_H

#include "vector_store.h"

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rag
{
struct SemanticCacheValue
{
    std::string answer;
    std::vector<SearchResult> sources;
    bool used_knowledge = false;
};

class SemanticCache
{
public:
    SemanticCache(std::size_t capacity, float threshold, std::chrono::seconds ttl);
    std::optional<SemanticCacheValue> lookup(const std::vector<float> &query);
    void put(std::vector<float> query, SemanticCacheValue value);
    std::size_t size() const;

private:
    struct Entry
    {
        std::vector<float> query;
        SemanticCacheValue value;
        std::chrono::steady_clock::time_point expires_at;
    };
    static float cosine(const std::vector<float> &left, const std::vector<float> &right);
    void remove_expired(std::chrono::steady_clock::time_point now);

    std::size_t capacity_;
    float threshold_;
    std::chrono::seconds ttl_;
    mutable std::mutex mutex_;
    std::deque<Entry> entries_;
};

class CircuitBreaker
{
public:
    CircuitBreaker(unsigned int failure_threshold, std::chrono::seconds cooldown);
    bool allow_request();
    void record_success();
    void record_failure();
    bool open() const;

private:
    unsigned int failure_threshold_;
    std::chrono::seconds cooldown_;
    mutable std::mutex mutex_;
    unsigned int failures_ = 0;
    std::chrono::steady_clock::time_point open_until_{};
};
}

#endif
