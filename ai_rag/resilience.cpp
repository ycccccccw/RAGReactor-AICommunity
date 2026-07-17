#include "resilience.h"

#include <algorithm>
#include <cmath>

namespace rag
{
SemanticCache::SemanticCache(std::size_t capacity, float threshold,
                             std::chrono::seconds ttl)
    : capacity_(capacity), threshold_(threshold), ttl_(ttl) {}

float SemanticCache::cosine(const std::vector<float> &left,
                            const std::vector<float> &right)
{
    if (left.empty() || left.size() != right.size()) return -1.0f;
    double dot = 0.0, left_norm = 0.0, right_norm = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        dot += static_cast<double>(left[i]) * right[i];
        left_norm += static_cast<double>(left[i]) * left[i];
        right_norm += static_cast<double>(right[i]) * right[i];
    }
    if (left_norm == 0.0 || right_norm == 0.0) return -1.0f;
    return static_cast<float>(dot / std::sqrt(left_norm * right_norm));
}

void SemanticCache::remove_expired(std::chrono::steady_clock::time_point now)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
        [now](const Entry &entry) { return entry.expires_at <= now; }), entries_.end());
}

std::optional<SemanticCacheValue> SemanticCache::lookup(const std::vector<float> &query)
{
    std::lock_guard<std::mutex> guard(mutex_);
    const auto now = std::chrono::steady_clock::now();
    remove_expired(now);
    float best = threshold_;
    auto best_it = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
    {
        const float score = cosine(query, it->query);
        if (score >= best)
        {
            best = score;
            best_it = it;
        }
    }
    if (best_it == entries_.end()) return std::nullopt;
    SemanticCacheValue value = best_it->value;
    Entry refreshed = std::move(*best_it);
    entries_.erase(best_it);
    entries_.push_back(std::move(refreshed));
    return value;
}

void SemanticCache::put(std::vector<float> query, SemanticCacheValue value)
{
    if (capacity_ == 0 || query.empty() || value.answer.empty()) return;
    std::lock_guard<std::mutex> guard(mutex_);
    remove_expired(std::chrono::steady_clock::now());
    if (entries_.size() >= capacity_) entries_.pop_front();
    entries_.push_back({std::move(query), std::move(value),
                        std::chrono::steady_clock::now() + ttl_});
}

std::size_t SemanticCache::size() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return entries_.size();
}

CircuitBreaker::CircuitBreaker(unsigned int failure_threshold,
                               std::chrono::seconds cooldown)
    : failure_threshold_(failure_threshold), cooldown_(cooldown) {}

bool CircuitBreaker::allow_request()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (open_until_ == std::chrono::steady_clock::time_point{}) return true;
    if (std::chrono::steady_clock::now() < open_until_) return false;
    open_until_ = std::chrono::steady_clock::time_point{};
    failures_ = failure_threshold_ > 0 ? failure_threshold_ - 1 : 0;
    return true;
}

void CircuitBreaker::record_success()
{
    std::lock_guard<std::mutex> guard(mutex_);
    failures_ = 0;
    open_until_ = std::chrono::steady_clock::time_point{};
}

void CircuitBreaker::record_failure()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (++failures_ >= failure_threshold_)
        open_until_ = std::chrono::steady_clock::now() + cooldown_;
}

bool CircuitBreaker::open() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return open_until_ != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() < open_until_;
}
}
