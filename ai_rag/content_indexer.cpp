#include "content_indexer.h"

#include <exception>

namespace rag
{
ContentIndexer::ContentIndexer(const EmbeddingProvider &provider,
                               TextSplitter splitter)
    : provider_(provider), splitter_(std::move(splitter)) {}

bool ContentIndexer::embed_chunks(const Document &document,
                                  std::vector<DocumentChunk> &chunks,
                                  std::string *error) const
{
    try
    {
        chunks = splitter_.split(document);
        for (DocumentChunk &chunk : chunks)
            chunk.embedding = provider_.embed(chunk.text);
        return true;
    }
    catch (const std::exception &exception)
    {
        if (error) *error = exception.what();
        return false;
    }
}

bool ContentIndexer::build(const std::vector<Document> &documents,
                           VectorStore &store, IndexBuildStats &stats,
                           std::string *error) const
{
    stats = IndexBuildStats();
    VectorStore built(provider_.dimension());
    for (const Document &document : documents)
    {
        std::vector<DocumentChunk> chunks;
        if (!embed_chunks(document, chunks, error)) return false;
        for (DocumentChunk &chunk : chunks)
        {
            if (!built.add(std::move(chunk), error)) return false;
            ++stats.chunks;
        }
        ++stats.documents;
    }
    store = std::move(built);
    return true;
}

bool ContentIndexer::upsert(const Document &document, VectorStore &store,
                            std::size_t *chunk_count, std::string *error) const
{
    std::vector<DocumentChunk> chunks;
    if (!embed_chunks(document, chunks, error)) return false;
    const std::string source_id = document.source_id.empty() ? document.id : document.source_id;
    VectorStore updated = store;
    updated.remove_source(document.source_type, source_id);
    for (DocumentChunk &chunk : chunks)
        if (!updated.add(std::move(chunk), error)) return false;
    store = std::move(updated);
    if (chunk_count) *chunk_count = chunks.size();
    return true;
}

void ContentIndexer::remove(const std::string &source_type,
                            const std::string &source_id,
                            VectorStore &store) const
{
    store.remove_source(source_type, source_id);
}
}
