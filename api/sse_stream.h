#ifndef RAGREACTOR_SSE_STREAM_H
#define RAGREACTOR_SSE_STREAM_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

class SseStream
{
public:
    explicit SseStream(std::size_t max_queued_bytes = 256 * 1024);

    bool push(std::string chunk);
    bool try_pop(std::string &chunk);
    void finish();
    void cancel();
    bool canceled() const { return canceled_.load(); }
    const std::atomic<bool> &cancellation_flag() const { return canceled_; }
    bool finished_and_empty() const;
    std::size_t queued_bytes() const;

private:
    const std::size_t max_queued_bytes_;
    mutable std::mutex mutex_;
    std::condition_variable space_available_;
    std::deque<std::string> chunks_;
    std::size_t queued_bytes_;
    bool finished_;
    std::atomic<bool> canceled_;
};

#endif
