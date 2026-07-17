#include "knowledge_indexer.h"

namespace rag
{
KnowledgeIndexer::KnowledgeIndexer(const EmbeddingProvider &provider,
                                   TextSplitter splitter)
    : provider_(provider), splitter_(std::move(splitter)) {}

bool KnowledgeIndexer::build(const std::string &document_directory,
                             VectorStore &store, IndexBuildStats &stats,
                             std::string *error) const
{
    stats = IndexBuildStats();
    std::vector<Document> documents;
    if (!DocumentLoader::load_directory(document_directory, documents, error))
        return false;

    VectorStore built(provider_.dimension());
    for (const Document &document : documents)
    {
        std::vector<DocumentChunk> chunks = splitter_.split(document);
        for (DocumentChunk &chunk : chunks)
        {
            chunk.embedding = provider_.embed(chunk.text);
            if (!built.add(std::move(chunk), error)) return false;
            ++stats.chunks;
        }
    }
    stats.documents = documents.size();
    store = std::move(built);
    return true;
}
}
