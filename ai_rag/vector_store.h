#ifndef RAGREACTOR_VECTOR_STORE_H
#define RAGREACTOR_VECTOR_STORE_H

#include "rag_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{
class VectorStore
{
public:
    explicit VectorStore(std::size_t dimension = 0);

    bool add(DocumentChunk chunk, std::string *error = nullptr);
    std::vector<SearchResult> search(const std::vector<float> &query,
                                     std::size_t top_k,
                                     const ContentFilter &filter = ContentFilter()) const;
    void remove_source(const std::string &source_type, const std::string &source_id);
    bool save(const std::string &path, std::string *error = nullptr) const;
    bool load(const std::string &path, std::string *error = nullptr);
    void clear();

    std::size_t size() const { return chunks_.size(); }
    std::size_t dimension() const { return dimension_; }
    const std::vector<DocumentChunk> &chunks() const { return chunks_; }

    static float cosine_similarity(const std::vector<float> &left,
                                   const std::vector<float> &right);

private:
    std::size_t dimension_;
    std::vector<DocumentChunk> chunks_;
};
}

#endif
