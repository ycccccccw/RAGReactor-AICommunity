#include "../ai_rag/hnsw_index.h"
#include "../ai_rag/hybrid_retriever.h"

#include <cassert>
#include <iostream>

namespace
{
rag::DocumentChunk chunk(const std::string &source, std::size_t index,
                         const std::string &text, std::vector<float> embedding)
{
    rag::DocumentChunk value;
    value.document_id = source;
    value.source = source;
    value.chunk_index = index;
    value.text = text;
    value.embedding = std::move(embedding);
    return value;
}
}

int main()
{
    rag::VectorStore store(3);
    std::string error;
    assert(store.add(chunk("reactor.md", 0,
        "pipefd 写入信号后，epoll 通过 EPOLLIN 通知主循环。", {1.0f, 0.0f, 0.0f}), &error));
    assert(store.add(chunk("security.md", 0,
        "密码使用 PBKDF2 和独立随机盐保存。", {0.0f, 1.0f, 0.0f}), &error));
    assert(store.add(chunk("pool.md", 0,
        "线程池复用工作线程处理 HTTP 请求。", {0.0f, 0.0f, 1.0f}), &error));

    rag::HnswIndex hnsw(3, 8, 50, 20);
    assert(hnsw.build(store, &error));
    const auto vector_results = hnsw.search({0.99f, 0.01f, 0.0f}, 2);
    assert(!vector_results.empty());
    assert(vector_results.front().chunk.source == "reactor.md");

    rag::Bm25Index bm25;
    bm25.build(store);
    const auto keyword_results = bm25.search("pipefd EPOLLIN", 2);
    assert(!keyword_results.empty());
    assert(keyword_results.front().chunk.source == "reactor.md");

    rag::HybridRetriever hybrid(store, &hnsw, &bm25, 3);
    const auto fused = hybrid.search("PBKDF2 随机盐", {0.0f, 1.0f, 0.0f}, 2);
    assert(!fused.empty());
    assert(fused.front().chunk.source == "security.md");

    std::cout << "retrieval_upgrade_test: all checks passed\n";
    return 0;
}
