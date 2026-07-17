#include "sse_stream.h"

#include <utility>

SseStream::SseStream(std::size_t max_queued_bytes)
    : max_queued_bytes_(max_queued_bytes), queued_bytes_(0), finished_(false),
      canceled_(false) {}

bool SseStream::push(std::string chunk)
{
    if (chunk.empty()) return !canceled();
    std::unique_lock<std::mutex> lock(mutex_);
    space_available_.wait(lock, [this, &chunk] {
        return canceled_.load() || finished_ || chunks_.empty() ||
               queued_bytes_ + chunk.size() <= max_queued_bytes_;
    });
    if (canceled_.load() || finished_) return false;
    chunks_.push_back(std::move(chunk));
    queued_bytes_ += chunks_.back().size();
    return true;
}

bool SseStream::try_pop(std::string &chunk)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (chunks_.empty()) return false;
    chunk = std::move(chunks_.front());
    queued_bytes_ -= chunk.size();
    chunks_.pop_front();
    space_available_.notify_one();
    return true;
}

void SseStream::finish()
{
    std::lock_guard<std::mutex> lock(mutex_);
    finished_ = true;
    space_available_.notify_all();
}

void SseStream::cancel()
{
    canceled_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
    queued_bytes_ = 0;
    space_available_.notify_all();
}

bool SseStream::finished_and_empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_ && chunks_.empty();
}

std::size_t SseStream::queued_bytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queued_bytes_;
}
