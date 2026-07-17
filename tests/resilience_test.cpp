#include "../ai_rag/resilience.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    rag::SemanticCache cache(2, 0.95f, std::chrono::seconds(60));
    rag::SemanticCacheValue value;
    value.answer = "epoll answer";
    value.used_knowledge = true;
    cache.put({1.0f, 0.0f}, value);
    assert(cache.lookup({0.999f, 0.01f}).has_value());
    assert(!cache.lookup({0.0f, 1.0f}).has_value());

    rag::CircuitBreaker circuit(2, std::chrono::seconds(1));
    assert(circuit.allow_request());
    circuit.record_failure();
    assert(circuit.allow_request());
    circuit.record_failure();
    assert(!circuit.allow_request());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(circuit.allow_request());
    circuit.record_success();
    assert(!circuit.open());

    std::cout << "resilience_test: all checks passed\n";
    return 0;
}
