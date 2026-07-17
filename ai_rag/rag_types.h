#ifndef RAGREACTOR_RAG_TYPES_H
#define RAGREACTOR_RAG_TYPES_H

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{
struct Document
{
    std::string id;
    std::string source;
    std::string content;
};

struct DocumentChunk
{
    std::string document_id;
    std::string source;
    std::size_t chunk_index = 0;
    std::string text;
    std::vector<float> embedding;
};

struct SearchResult
{
    DocumentChunk chunk;
    float score = 0.0f;
};
}

#endif
