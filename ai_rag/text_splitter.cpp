#include "text_splitter.h"

#include <stdexcept>

namespace rag
{
namespace
{
bool is_utf8_continuation(unsigned char byte)
{
    return (byte & 0xC0u) == 0x80u;
}

std::vector<std::size_t> utf8_boundaries(const std::string &text)
{
    std::vector<std::size_t> boundaries;
    boundaries.reserve(text.size() + 1);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (!is_utf8_continuation(static_cast<unsigned char>(text[i])))
            boundaries.push_back(i);
    }
    boundaries.push_back(text.size());
    return boundaries;
}

std::string trim_copy(const std::string &text)
{
    const char *spaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(spaces);
    return text.substr(first, last - first + 1);
}
}

TextSplitter::TextSplitter(std::size_t chunk_size, std::size_t overlap)
    : chunk_size_(chunk_size), overlap_(overlap)
{
    if (chunk_size_ == 0)
        throw std::invalid_argument("chunk_size must be greater than zero");
    if (overlap_ >= chunk_size_)
        throw std::invalid_argument("overlap must be smaller than chunk_size");
}

std::vector<DocumentChunk> TextSplitter::split(const Document &document) const
{
    std::vector<DocumentChunk> chunks;
    const std::vector<std::size_t> boundaries = utf8_boundaries(document.content);
    const std::size_t character_count = boundaries.empty() ? 0 : boundaries.size() - 1;
    if (character_count == 0) return chunks;

    const std::size_t step = chunk_size_ - overlap_;
    std::size_t start_character = 0;
    while (start_character < character_count)
    {
        const std::size_t end_character =
            std::min(start_character + chunk_size_, character_count);
        const std::size_t begin_byte = boundaries[start_character];
        const std::size_t end_byte = boundaries[end_character];
        std::string text = trim_copy(
            document.content.substr(begin_byte, end_byte - begin_byte));

        if (!text.empty())
        {
            DocumentChunk chunk;
            chunk.document_id = document.id;
            chunk.source = document.source;
            chunk.chunk_index = chunks.size();
            chunk.text = std::move(text);
            chunk.source_type = document.source_type;
            chunk.source_id = document.source_id.empty() ? document.id : document.source_id;
            chunk.author = document.author;
            chunk.created_at = document.created_at;
            chunk.status = document.status;
            chunk.trust_level = document.trust_level;
            chunk.content_version = document.content_version;
            chunks.push_back(std::move(chunk));
        }

        if (end_character == character_count) break;
        start_character += step;
    }
    return chunks;
}
}
