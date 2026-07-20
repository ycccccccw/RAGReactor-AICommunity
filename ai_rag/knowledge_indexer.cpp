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

    for (Document &document : documents)
    {
        document.source_type = "knowledge";
        document.source_id = document.id;
        document.status = "ready";
        document.trust_level = "curated_knowledge";
    }
    ContentIndexer indexer(provider_, splitter_);
    return indexer.build(documents, store, stats, error);
}
}
