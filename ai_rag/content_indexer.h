#ifndef RAGREACTOR_CONTENT_INDEXER_H
#define RAGREACTOR_CONTENT_INDEXER_H

#include "embedding_provider.h"
#include "text_splitter.h"
#include "vector_store.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{
struct IndexBuildStats
{
    std::size_t documents = 0;
    std::size_t chunks = 0;
};

class ContentIndexer
{
public:
    ContentIndexer(const EmbeddingProvider &provider, TextSplitter splitter);

    bool build(const std::vector<Document> &documents, VectorStore &store,
               IndexBuildStats &stats, std::string *error = nullptr) const;
    bool upsert(const Document &document, VectorStore &store,
                std::size_t *chunk_count = nullptr,
                std::string *error = nullptr) const;
    void remove(const std::string &source_type, const std::string &source_id,
                VectorStore &store) const;

private:
    bool embed_chunks(const Document &document,
                      std::vector<DocumentChunk> &chunks,
                      std::string *error) const;

    const EmbeddingProvider &provider_;
    TextSplitter splitter_;
};
}

#endif

