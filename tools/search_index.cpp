#include "../ai_rag/embedding_provider.h"
#include "../ai_rag/vector_store.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "Usage: " << argv[0] << " <index-file> <question> [top-k]\n";
        return 2;
    }

    const std::size_t top_k = argc == 4 ? std::strtoul(argv[3], nullptr, 10) : 3;
    rag::VectorStore store;
    std::string error;
    if (!store.load(argv[1], &error))
    {
        std::cerr << "Index load failed: " << error << '\n';
        return 1;
    }

    rag::MockEmbeddingProvider provider(store.dimension());
    const std::vector<rag::SearchResult> results =
        store.search(provider.embed(argv[2]), top_k);
    if (results.empty())
    {
        std::cout << "No matching chunks found.\n";
        return 0;
    }

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const rag::SearchResult &result = results[i];
        std::cout << "Rank " << i + 1
                  << " | score=" << std::fixed << std::setprecision(4) << result.score
                  << " | source=" << result.chunk.source
                  << " | chunk=" << result.chunk.chunk_index << '\n'
                  << result.chunk.text << "\n\n";
    }
    return 0;
}
