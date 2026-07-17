#include "vector_store.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>

namespace fs = std::filesystem;

namespace rag
{
namespace
{
const char MAGIC[8] = {'R', 'A', 'G', 'V', 'E', 'C', '0', '1'};
const std::uint32_t FORMAT_VERSION = 1;
const std::uint64_t MAX_RECORDS = 10000000;
const std::uint64_t MAX_STRING_BYTES = 16 * 1024 * 1024;
const std::uint64_t MAX_DIMENSION = 65536;

void set_error(std::string *error, const std::string &message)
{
    if (error) *error = message;
}

template <typename T>
bool write_value(std::ofstream &output, const T &value)
{
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
    return output.good();
}

template <typename T>
bool read_value(std::ifstream &input, T &value)
{
    input.read(reinterpret_cast<char *>(&value), sizeof(value));
    return input.good();
}

bool write_string(std::ofstream &output, const std::string &value)
{
    const std::uint64_t size = value.size();
    return write_value(output, size) &&
           (size == 0 || (output.write(value.data(), size), output.good()));
}

bool read_string(std::ifstream &input, std::string &value)
{
    std::uint64_t size = 0;
    if (!read_value(input, size) || size > MAX_STRING_BYTES) return false;
    value.resize(static_cast<std::size_t>(size));
    if (size > 0) input.read(&value[0], size);
    return input.good();
}
}

VectorStore::VectorStore(std::size_t dimension) : dimension_(dimension) {}

bool VectorStore::add(DocumentChunk chunk, std::string *error)
{
    if (chunk.embedding.empty())
    {
        set_error(error, "chunk embedding must not be empty");
        return false;
    }
    if (dimension_ == 0) dimension_ = chunk.embedding.size();
    if (chunk.embedding.size() != dimension_)
    {
        set_error(error, "chunk embedding dimension does not match vector store");
        return false;
    }
    chunks_.push_back(std::move(chunk));
    return true;
}

float VectorStore::cosine_similarity(const std::vector<float> &left,
                                     const std::vector<float> &right)
{
    if (left.empty() || left.size() != right.size()) return 0.0f;
    double dot = 0.0;
    double left_norm = 0.0;
    double right_norm = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        dot += static_cast<double>(left[i]) * right[i];
        left_norm += static_cast<double>(left[i]) * left[i];
        right_norm += static_cast<double>(right[i]) * right[i];
    }
    if (left_norm == 0.0 || right_norm == 0.0) return 0.0f;
    return static_cast<float>(dot / std::sqrt(left_norm * right_norm));
}

std::vector<SearchResult> VectorStore::search(const std::vector<float> &query,
                                               std::size_t top_k) const
{
    if (top_k == 0 || query.size() != dimension_) return {};

    struct Candidate
    {
        float score;
        std::size_t index;
    };
    struct MinScore
    {
        bool operator()(const Candidate &left, const Candidate &right) const
        {
            if (left.score != right.score) return left.score > right.score;
            return left.index > right.index;
        }
    };

    std::priority_queue<Candidate, std::vector<Candidate>, MinScore> heap;
    for (std::size_t i = 0; i < chunks_.size(); ++i)
    {
        Candidate candidate{cosine_similarity(query, chunks_[i].embedding), i};
        if (heap.size() < top_k)
            heap.push(candidate);
        else if (candidate.score > heap.top().score)
        {
            heap.pop();
            heap.push(candidate);
        }
    }

    std::vector<SearchResult> results;
    results.reserve(heap.size());
    while (!heap.empty())
    {
        const Candidate candidate = heap.top();
        heap.pop();
        results.push_back(SearchResult{chunks_[candidate.index], candidate.score});
    }
    std::sort(results.begin(), results.end(), [](const SearchResult &left,
                                                  const SearchResult &right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.chunk.source != right.chunk.source)
            return left.chunk.source < right.chunk.source;
        return left.chunk.chunk_index < right.chunk.chunk_index;
    });
    return results;
}

bool VectorStore::save(const std::string &path, std::string *error) const
{
    if (dimension_ == 0)
    {
        set_error(error, "cannot save a vector store with zero dimension");
        return false;
    }

    const fs::path output_path(path);
    std::error_code ec;
    if (!output_path.parent_path().empty())
        fs::create_directories(output_path.parent_path(), ec);
    if (ec)
    {
        set_error(error, "failed to create index directory: " + ec.message());
        return false;
    }

    const std::string temporary = path + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        set_error(error, "failed to open index for writing: " + temporary);
        return false;
    }

    output.write(MAGIC, sizeof(MAGIC));
    const std::uint64_t dimension = dimension_;
    const std::uint64_t count = chunks_.size();
    if (!write_value(output, FORMAT_VERSION) || !write_value(output, dimension) ||
        !write_value(output, count))
    {
        set_error(error, "failed to write vector index header");
        return false;
    }

    for (const DocumentChunk &chunk : chunks_)
    {
        const std::uint64_t chunk_index = chunk.chunk_index;
        if (!write_string(output, chunk.document_id) ||
            !write_string(output, chunk.source) ||
            !write_value(output, chunk_index) ||
            !write_string(output, chunk.text))
        {
            set_error(error, "failed to write vector index metadata");
            return false;
        }
        output.write(reinterpret_cast<const char *>(chunk.embedding.data()),
                     chunk.embedding.size() * sizeof(float));
        if (!output.good())
        {
            set_error(error, "failed to write vector index embedding");
            return false;
        }
    }
    output.close();
    if (!output)
    {
        set_error(error, "failed to flush vector index");
        return false;
    }

    fs::rename(temporary, path, ec);
    if (ec)
    {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    if (ec)
    {
        set_error(error, "failed to publish vector index: " + ec.message());
        return false;
    }
    return true;
}

bool VectorStore::load(const std::string &path, std::string *error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        set_error(error, "failed to open vector index: " + path);
        return false;
    }

    char magic[sizeof(MAGIC)] = {};
    input.read(magic, sizeof(magic));
    std::uint32_t version = 0;
    std::uint64_t dimension = 0;
    std::uint64_t count = 0;
    if (!input.good() || !std::equal(std::begin(MAGIC), std::end(MAGIC), magic) ||
        !read_value(input, version) || version != FORMAT_VERSION ||
        !read_value(input, dimension) || dimension == 0 || dimension > MAX_DIMENSION ||
        !read_value(input, count) || count > MAX_RECORDS)
    {
        set_error(error, "invalid or unsupported vector index header");
        return false;
    }

    std::vector<DocumentChunk> loaded;
    loaded.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        DocumentChunk chunk;
        std::uint64_t chunk_index = 0;
        if (!read_string(input, chunk.document_id) || !read_string(input, chunk.source) ||
            !read_value(input, chunk_index) || !read_string(input, chunk.text))
        {
            set_error(error, "invalid vector index record metadata");
            return false;
        }
        chunk.chunk_index = static_cast<std::size_t>(chunk_index);
        chunk.embedding.resize(static_cast<std::size_t>(dimension));
        input.read(reinterpret_cast<char *>(chunk.embedding.data()),
                   chunk.embedding.size() * sizeof(float));
        if (!input.good())
        {
            set_error(error, "invalid vector index embedding data");
            return false;
        }
        loaded.push_back(std::move(chunk));
    }

    char trailing = 0;
    if (input.read(&trailing, 1))
    {
        set_error(error, "vector index contains unexpected trailing data");
        return false;
    }

    dimension_ = static_cast<std::size_t>(dimension);
    chunks_.swap(loaded);
    return true;
}

void VectorStore::clear()
{
    chunks_.clear();
    dimension_ = 0;
}
}
