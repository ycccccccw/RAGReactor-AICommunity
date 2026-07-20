#ifndef RAGREACTOR_QUESTION_INTEREST_STORE_H
#define RAGREACTOR_QUESTION_INTEREST_STORE_H

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class QuestionInterestStore
{
public:
    static QuestionInterestStore &instance();
    void remember(const std::string &username, const std::vector<float> &embedding);
    std::vector<float> current(const std::string &username) const;

private:
    struct Signal { std::vector<float> embedding; std::chrono::steady_clock::time_point created; };
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<Signal>> signals_;
};

#endif
