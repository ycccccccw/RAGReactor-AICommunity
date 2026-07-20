#include "../ai_rag/hnsw_index.h"
#include "../ai_rag/hybrid_retriever.h"

#include <cassert>
#include <iostream>

namespace
{
rag::DocumentChunk chunk(const std::string &source, std::size_t index,
                         const std::string &text, std::vector<float> embedding,
                         const std::string &source_type = "knowledge",
                         const std::string &status = "ready")
{
    rag::DocumentChunk value;
    value.document_id = source;
    value.source = source;
    value.chunk_index = index;
    value.text = text;
    value.embedding = std::move(embedding);
    value.source_type = source_type;
    value.source_id = source;
    value.status = status;
    value.trust_level = source_type == "knowledge" ?
        "curated_knowledge" : "community_unverified";
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
    assert(store.add(chunk("post-42", 0,
        "社区用户分享了 Reactor 调优经验。", {0.98f, 0.02f, 0.0f}, "community"), &error));
    assert(store.add(chunk("post-43", 0,
        "这条社区内容已被屏蔽。", {1.0f, 0.0f, 0.0f}, "community", "blocked"), &error));

    rag::HnswIndex hnsw(3, 8, 50, 20);
    assert(hnsw.build(store, &error));
    const auto vector_results = hnsw.search({0.99f, 0.01f, 0.0f}, 2);
    assert(!vector_results.empty());
    assert(vector_results.front().chunk.source == "reactor.md");

    rag::ContentFilter knowledge_only;
    knowledge_only.source_types = {"knowledge"};
    const auto knowledge_results = hnsw.search({1.0f, 0.0f, 0.0f}, 10, knowledge_only);
    assert(!knowledge_results.empty());
    for (const auto &result : knowledge_results)
        assert(result.chunk.source_type == "knowledge" && result.chunk.status == "ready");

    rag::ContentFilter community_only;
    community_only.source_types = {"community"};
    const auto community_results = hnsw.search({1.0f, 0.0f, 0.0f}, 10, community_only);
    assert(community_results.size() == 1);
    assert(community_results.front().chunk.source == "post-42");

    rag::Bm25Index bm25;
    bm25.build(store);
    const auto keyword_results = bm25.search("pipefd EPOLLIN", 2);
    assert(!keyword_results.empty());
    assert(keyword_results.front().chunk.source == "reactor.md");
    const auto community_keywords = bm25.search("社区 Reactor", 10, community_only);
    assert(community_keywords.size() == 1);
    assert(community_keywords.front().chunk.source == "post-42");

    rag::HybridRetriever hybrid(store, &hnsw, &bm25, 3);
    const auto fused = hybrid.search("PBKDF2 随机盐", {0.0f, 1.0f, 0.0f}, 2);
    assert(!fused.empty());
    assert(fused.front().chunk.source == "security.md");

    const auto fused_knowledge = hybrid.search(
        "Reactor", {1.0f, 0.0f, 0.0f}, 10, knowledge_only);
    for (const auto &result : fused_knowledge)
        assert(result.chunk.source_type == "knowledge");
    const auto fused_community = hybrid.search(
        "Reactor 社区", {1.0f, 0.0f, 0.0f}, 10, community_only);
    assert(fused_community.size() == 1);
    assert(fused_community.front().chunk.source == "post-42");

    rag::ContentFilter both;
    both.source_types = {"knowledge", "community"};
    const auto fused_both = hybrid.search("Reactor", {1.0f, 0.0f, 0.0f}, 10, both);
    bool saw_knowledge = false, saw_community = false;
    for (const auto &result : fused_both)
    {
        saw_knowledge = saw_knowledge || result.chunk.source_type == "knowledge";
        saw_community = saw_community || result.chunk.source_type == "community";
        assert(result.chunk.status == "ready");
    }
    assert(saw_knowledge && saw_community);

    std::cout << "retrieval_upgrade_test: all checks passed\n";
    return 0;
}
