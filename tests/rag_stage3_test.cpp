#include "../ai_rag/prompt_builder.h"

#include <cassert>
#include <iostream>

int main()
{
    rag::SearchResult first;
    first.chunk.source = "reactor.md";
    first.chunk.chunk_index = 2;
    first.chunk.text = "pipefd 将信号转换成 epoll 可监听的可读事件。";
    first.score = 0.88f;

    rag::PromptBuilder builder(2048);
    const std::string prompt = builder.build("pipefd 有什么作用？", {first});
    assert(prompt.find("用户问题：pipefd 有什么作用？") != std::string::npos);
    assert(prompt.find("[来源1]") != std::string::npos);
    assert(prompt.find("reactor.md") != std::string::npos);
    assert(prompt.find(first.chunk.text) != std::string::npos);

    rag::SearchResult very_large = first;
    very_large.chunk.text.assign(5000, 'x');
    const std::string limited = builder.build("question", {very_large});
    assert(limited.size() <= 2048);
    assert(limited.find("用户问题：question") != std::string::npos);

    std::cout << "rag_stage3_test: all checks passed\n";
    return 0;
}
