#include "../ai_rag/document_loader.h"
#include "../ai_rag/embedding_provider.h"
#include "../ai_rag/knowledge_indexer.h"
#include "../ai_rag/text_splitter.h"
#include "../ai_rag/vector_store.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{
void test_document_loader()
{
    std::vector<rag::Document> documents;
    std::string error;
    assert(rag::DocumentLoader::load_directory("knowledge/documents", documents, &error));
    assert(documents.size() == 3);
    assert(documents[0].source == "reactor.md");
    assert(!documents[0].content.empty());
    assert(!rag::DocumentLoader::is_supported("document.pdf"));
}

void test_utf8_splitter_and_overlap()
{
    rag::Document document;
    document.id = "utf8";
    document.source = "utf8.txt";
    document.content = "甲乙丙丁戊己庚辛";

    rag::TextSplitter splitter(4, 2);
    const std::vector<rag::DocumentChunk> chunks = splitter.split(document);
    assert(chunks.size() == 3);
    assert(chunks[0].text == "甲乙丙丁");
    assert(chunks[1].text == "丙丁戊己");
    assert(chunks[2].text == "戊己庚辛");
    assert(chunks[2].chunk_index == 2);

    bool rejected = false;
    try { rag::TextSplitter invalid(4, 4); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
}

void test_mock_embedding()
{
    rag::MockEmbeddingProvider provider(128);
    const std::vector<float> first = provider.embed("epoll pipefd 信号");
    const std::vector<float> second = provider.embed("epoll pipefd 信号");
    assert(first == second);
    assert(first.size() == 128);
    double norm = 0.0;
    for (float value : first) norm += value * value;
    assert(std::fabs(norm - 1.0) < 0.0001);
}

void test_exact_top_k()
{
    rag::VectorStore store(3);
    rag::DocumentChunk first;
    first.source = "first.txt";
    first.text = "first";
    first.embedding = {1.0f, 0.0f, 0.0f};
    rag::DocumentChunk second;
    second.source = "second.txt";
    second.text = "second";
    second.embedding = {0.8f, 0.2f, 0.0f};
    rag::DocumentChunk third;
    third.source = "third.txt";
    third.text = "third";
    third.embedding = {0.0f, 1.0f, 0.0f};
    assert(store.add(std::move(first)));
    assert(store.add(std::move(second)));
    assert(store.add(std::move(third)));

    const std::vector<rag::SearchResult> results = store.search({1.0f, 0.0f, 0.0f}, 2);
    assert(results.size() == 2);
    assert(results[0].chunk.source == "first.txt");
    assert(results[1].chunk.source == "second.txt");
    assert(results[0].score >= results[1].score);
}

void test_index_persistence_and_retrieval()
{
    rag::MockEmbeddingProvider provider(256);
    rag::KnowledgeIndexer indexer(provider, rag::TextSplitter(120, 20));
    rag::VectorStore store;
    rag::IndexBuildStats stats;
    std::string error;
    assert(indexer.build("knowledge/documents", store, stats, &error));
    assert(stats.documents == 3);
    assert(stats.chunks >= 3);

    const std::vector<float> query = provider.embed("pipefd 如何通过 epoll 处理 SIGTERM 信号");
    const std::vector<rag::SearchResult> results = store.search(query, 3);
    assert(!results.empty());
    assert(results.front().chunk.source == "reactor.md");
    assert(results.front().chunk.text.find("pipefd") != std::string::npos);

    const fs::path index_path = fs::temp_directory_path() / "rag_stage2_test.index";
    assert(store.save(index_path.string(), &error));
    rag::VectorStore loaded;
    assert(loaded.load(index_path.string(), &error));
    assert(loaded.size() == store.size());
    assert(loaded.dimension() == store.dimension());
    const std::vector<rag::SearchResult> loaded_results = loaded.search(query, 1);
    assert(loaded_results.size() == 1);
    assert(loaded_results.front().chunk.source == "reactor.md");
    fs::remove(index_path);
}
}

int main()
{
    test_document_loader();
    test_utf8_splitter_and_overlap();
    test_mock_embedding();
    test_exact_top_k();
    test_index_persistence_and_retrieval();
    std::cout << "rag_stage2_test: all checks passed\n";
    return 0;
}
