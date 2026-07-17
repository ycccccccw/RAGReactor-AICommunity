#include "embedding_provider.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace rag
{
namespace
{
std::uint64_t fnv1a(const std::string &token)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : token)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<std::string> utf8_characters(const std::string &text)
{
    std::vector<std::string> characters;
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((lead & 0xE0u) == 0xC0u) length = 2;
        else if ((lead & 0xF0u) == 0xE0u) length = 3;
        else if ((lead & 0xF8u) == 0xF0u) length = 4;
        if (i + length > text.size()) length = 1;
        characters.push_back(text.substr(i, length));
        i += length;
    }
    return characters;
}

void add_feature(std::vector<float> &embedding, const std::string &feature, float weight)
{
    if (feature.empty()) return;
    const std::uint64_t hash = fnv1a(feature);
    const std::size_t index = static_cast<std::size_t>(hash % embedding.size());
    const float sign = (hash & (1ULL << 63)) ? -1.0f : 1.0f;
    embedding[index] += sign * weight;
}
}

MockEmbeddingProvider::MockEmbeddingProvider(std::size_t dimension)
    : dimension_(dimension)
{
    if (dimension_ < 8)
        throw std::invalid_argument("embedding dimension must be at least 8");
}

std::vector<float> MockEmbeddingProvider::embed(const std::string &text) const
{
    std::vector<float> embedding(dimension_, 0.0f);
    std::string ascii_word;
    const std::vector<std::string> characters = utf8_characters(text);

    for (std::size_t i = 0; i < characters.size(); ++i)
    {
        const std::string &character = characters[i];
        if (character.size() == 1 &&
            std::isalnum(static_cast<unsigned char>(character[0])))
        {
            ascii_word.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character[0]))));
        }
        else
        {
            add_feature(embedding, ascii_word, 2.0f);
            ascii_word.clear();
            if (character.size() > 1)
                add_feature(embedding, "c:" + character, 1.0f);
        }

        if (i + 1 < characters.size() &&
            (character.size() > 1 || characters[i + 1].size() > 1))
            add_feature(embedding, "b:" + character + characters[i + 1], 1.5f);
    }
    add_feature(embedding, ascii_word, 2.0f);

    double norm_squared = 0.0;
    for (float value : embedding) norm_squared += value * value;
    if (norm_squared > 0.0)
    {
        const float inverse_norm = static_cast<float>(1.0 / std::sqrt(norm_squared));
        for (float &value : embedding) value *= inverse_norm;
    }
    return embedding;
}
}
