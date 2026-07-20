#include "community_index_sync.h"

#include "../ai_rag/content_indexer.h"
#include "../ai_rag/hnsw_index.h"

#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string escape_sql(MYSQL *connection, const std::string &value)
{
    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long length = mysql_real_escape_string(
        connection, &escaped[0], value.data(), static_cast<unsigned long>(value.size()));
    escaped.resize(length);
    return escaped;
}

bool contains_source(const rag::VectorStore &store, const std::string &source_id,
                     unsigned long long version)
{
    for (const rag::DocumentChunk &chunk : store.chunks())
        if (chunk.source_type == "community" && chunk.source_id == source_id &&
            chunk.content_version == version && chunk.status == "ready")
            return true;
    return false;
}

bool update_registry(MYSQL *connection, const std::string &source_id,
                     const std::string &status, const std::string &model,
                     std::size_t dimension, const std::string &last_error,
                     std::string *error)
{
    std::ostringstream sql;
    sql << "UPDATE ai_content_registry SET index_status='" << escape_sql(connection, status)
        << "',embedding_model=";
    if (model.empty()) sql << "NULL";
    else sql << '\'' << escape_sql(connection, model) << '\'';
    sql << ",embedding_dimension=";
    if (dimension == 0) sql << "NULL";
    else sql << dimension;
    sql << ",last_error=";
    if (last_error.empty()) sql << "NULL";
    else sql << '\'' << escape_sql(connection, last_error.substr(0, 1000)) << '\'';
    if (status == "ready") sql << ",indexed_at=CURRENT_TIMESTAMP";
    sql << " WHERE source_type='community' AND source_id='"
        << escape_sql(connection, source_id) << '\'';
    if (mysql_query(connection, sql.str().c_str()) == 0) return true;
    if (error) *error = mysql_error(connection);
    return false;
}
}

CommunityIndexSync::CommunityIndexSync(MYSQL *connection,
                                       const rag::EmbeddingProvider &provider,
                                       std::string vector_path,
                                       std::string hnsw_path)
    : connection_(connection), provider_(provider), vector_path_(std::move(vector_path)),
      hnsw_path_(std::move(hnsw_path)) {}

bool CommunityIndexSync::run(CommunitySyncStats &stats, std::string *error) const
{
    stats = CommunitySyncStats();
    if (!connection_)
    {
        if (error) *error = "database connection is unavailable";
        return false;
    }

    rag::VectorStore store(provider_.dimension());
    bool full_rebuild = true;
    if (fs::exists(vector_path_))
    {
        std::string load_error;
        if (store.load(vector_path_, &load_error) && store.dimension() == provider_.dimension())
            full_rebuild = false;
        else
            store = rag::VectorStore(provider_.dimension());
    }

    const char *query =
        "SELECT r.source_id,r.index_status,r.search_enabled,r.content_version,"
        "p.username,p.content_text,p.created_at "
        "FROM ai_content_registry r LEFT JOIN user_posts p "
        "ON r.source_type='community' AND p.id=CAST(r.source_id AS UNSIGNED) "
        "WHERE r.source_type='community' ORDER BY r.id";
    if (mysql_query(connection_, query) != 0)
    {
        if (error) *error = mysql_error(connection_);
        return false;
    }
    MYSQL_RES *result = mysql_store_result(connection_);
    if (!result)
    {
        if (error) *error = mysql_error(connection_);
        return false;
    }

    struct RegistryRecord
    {
        std::string source_id;
        std::string status;
        bool search_enabled = true;
        unsigned long long version = 1;
        bool post_exists = false;
        std::string author;
        std::string content;
        std::string created_at;
    };
    std::vector<RegistryRecord> records;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr)
    {
        RegistryRecord record;
        record.source_id = row[0] ? row[0] : "";
        record.status = row[1] ? row[1] : "pending";
        record.search_enabled = row[2] && std::string(row[2]) != "0";
        record.version = row[3] ? std::strtoull(row[3], nullptr, 10) : 1;
        record.post_exists = row[4] != nullptr;
        record.author = row[4] ? row[4] : "";
        record.content = row[5] ? row[5] : "";
        record.created_at = row[6] ? row[6] : "";
        records.push_back(std::move(record));
    }
    mysql_free_result(result);

    rag::ContentIndexer indexer(provider_, rag::TextSplitter(500, 80));
    for (const RegistryRecord &record : records)
    {
        const bool removed = !record.post_exists || !record.search_enabled ||
                             record.status == "blocked" || record.status == "deleted";
        if (removed)
        {
            const std::size_t before = store.size();
            indexer.remove("community", record.source_id, store);
            if (store.size() != before) ++stats.removed;
            else ++stats.unchanged;
            continue;
        }
        if (!full_rebuild && record.status == "ready" &&
            contains_source(store, record.source_id, record.version))
        {
            ++stats.unchanged;
            continue;
        }

        rag::Document document;
        document.id = "community:" + record.source_id;
        document.source = "community-post:" + record.source_id;
        document.content = record.content;
        document.source_type = "community";
        document.source_id = record.source_id;
        document.author = record.author;
        document.created_at = record.created_at;
        document.status = "ready";
        document.trust_level = "community_unverified";
        document.content_version = record.version;
        if (document.content.empty())
        {
            const std::string message = "post has no indexable text";
            update_registry(connection_, record.source_id, "failed", "", 0, message, nullptr);
            ++stats.failed;
            continue;
        }

        update_registry(connection_, record.source_id, "processing", "", 0, "", nullptr);
        std::string index_error;
        if (!indexer.upsert(document, store, nullptr, &index_error))
        {
            update_registry(connection_, record.source_id, "failed", "", 0, index_error, nullptr);
            ++stats.failed;
            continue;
        }
        if (!update_registry(connection_, record.source_id, "ready", provider_.name(),
                             provider_.dimension(), "", error))
        {
            return false;
        }
        ++stats.indexed;
    }
    if (!store.save(vector_path_, error)) return false;
    if (store.size() == 0)
    {
        std::error_code ec;
        fs::remove(hnsw_path_, ec);
        return true;
    }
    rag::HnswIndex hnsw(store.dimension());
    if (!hnsw.build(store, error)) return false;
    return hnsw.save(hnsw_path_, error);
}
