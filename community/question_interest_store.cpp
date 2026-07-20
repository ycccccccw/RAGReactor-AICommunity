#include "question_interest_store.h"

#include <cmath>
#include <cstdlib>

QuestionInterestStore &QuestionInterestStore::instance()
{
    static QuestionInterestStore store;
    return store;
}

void QuestionInterestStore::remember(const std::string &username,
                                     const std::vector<float> &embedding)
{
    if (username.empty() || embedding.empty()) return;
    std::lock_guard<std::mutex> guard(mutex_);
    auto &values = signals_[username];
    values.push_back({embedding, std::chrono::steady_clock::now()});
    if (values.size() > 20) values.erase(values.begin(), values.end() - 20);
}

std::vector<float> QuestionInterestStore::current(const std::string &username) const
{
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = signals_.find(username);
    if (found == signals_.end() || found->second.empty()) return {};
    const char *configured = std::getenv("COMMUNITY_QUESTION_INTEREST_HALF_LIFE_MINUTES");
    double half_life = 60.0;
    try { if (configured && configured[0]) half_life = std::stod(configured); } catch (...) {}
    if (half_life <= 0.0) return {};
    const auto now = std::chrono::steady_clock::now();
    std::vector<double> sum(found->second.front().embedding.size(), 0.0);
    for (const Signal &signal : found->second)
    {
        if (signal.embedding.size() != sum.size()) continue;
        const double minutes = std::chrono::duration<double, std::ratio<60>>(
            now - signal.created).count();
        const double weight = std::pow(0.5, minutes / half_life);
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += signal.embedding[i] * weight;
    }
    double norm = 0.0;
    for (double value : sum) norm += value * value;
    if (norm == 0.0) return {};
    norm = std::sqrt(norm);
    std::vector<float> result(sum.size());
    for (std::size_t i = 0; i < sum.size(); ++i) result[i] = static_cast<float>(sum[i] / norm);
    return result;
}
