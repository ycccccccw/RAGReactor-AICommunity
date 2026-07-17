#include "../ai_rag/embedding_provider.h"
#include "../ai_rag/knowledge_indexer.h"
#include "../ai_rag/text_splitter.h"
#include "../ai_rag/vector_store.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 5)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <document-directory> <index-file> [chunk-size] [overlap]\n";
        return 2;
    }

    const std::size_t chunk_size = argc >= 4 ? std::strtoul(argv[3], nullptr, 10) : 500;
    const std::size_t overlap = argc >= 5 ? std::strtoul(argv[4], nullptr, 10) : 80;
    try
    {
        rag::MockEmbeddingProvider provider;
        rag::KnowledgeIndexer indexer(provider, rag::TextSplitter(chunk_size, overlap));
        rag::VectorStore store;
        rag::IndexBuildStats stats;
        std::string error;
        if (!indexer.build(argv[1], store, stats, &error) ||
            !store.save(argv[2], &error))
        {
            std::cerr << "Index build failed: " << error << '\n';
            return 1;
        }
        std::cout << "Index created: documents=" << stats.documents
                  << ", chunks=" << stats.chunks
                  << ", dimension=" << store.dimension()
                  << ", provider=" << provider.name() << '\n';
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Index build failed: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
