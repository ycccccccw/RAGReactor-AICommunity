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
    std::string source_type = "knowledge";
    std::string source_id;
    std::string author;
    std::string created_at;
    std::string status = "ready";
    std::string trust_level = "curated_knowledge";
    unsigned long long content_version = 1;
};

struct DocumentChunk
{
    std::string document_id;
    std::string source;
    std::size_t chunk_index = 0;
    std::string text;
    std::vector<float> embedding;
    std::string source_type = "knowledge";
    std::string source_id;
    std::string author;
    std::string created_at;
    std::string status = "ready";
    std::string trust_level = "curated_knowledge";
    unsigned long long content_version = 1;
};

struct ContentFilter
{
    std::vector<std::string> source_types;
    bool require_ready = true;

    bool matches(const DocumentChunk &chunk) const
    {
        if (require_ready && chunk.status != "ready") return false;
        if (source_types.empty()) return true;
        for (const std::string &type : source_types)
            if (chunk.source_type == type) return true;
        return false;
    }
};

struct SearchResult
{
    DocumentChunk chunk;
    float score = 0.0f;
};
}

#endif
