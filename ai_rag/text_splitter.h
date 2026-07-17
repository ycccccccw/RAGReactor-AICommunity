#ifndef RAGREACTOR_TEXT_SPLITTER_H
#define RAGREACTOR_TEXT_SPLITTER_H

#include "rag_types.h"

#include <cstddef>
#include <vector>

namespace rag
{
class TextSplitter
{
public:
    TextSplitter(std::size_t chunk_size = 500, std::size_t overlap = 80);

    std::vector<DocumentChunk> split(const Document &document) const;
    std::size_t chunk_size() const { return chunk_size_; }
    std::size_t overlap() const { return overlap_; }

private:
    std::size_t chunk_size_;
    std::size_t overlap_;
};
}

#endif
