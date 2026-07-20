#ifndef RAGREACTOR_KNOWLEDGE_INDEXER_H
#define RAGREACTOR_KNOWLEDGE_INDEXER_H

#include "document_loader.h"
#include "content_indexer.h"
#include "text_splitter.h"
#include "vector_store.h"

#include <cstddef>
#include <string>

namespace rag
{
class KnowledgeIndexer
{
public:
    KnowledgeIndexer(const EmbeddingProvider &provider, TextSplitter splitter);

    bool build(const std::string &document_directory, VectorStore &store,
               IndexBuildStats &stats, std::string *error = nullptr) const;

private:
    const EmbeddingProvider &provider_;
    TextSplitter splitter_;
};
}

#endif
