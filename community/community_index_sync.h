#ifndef RAGREACTOR_COMMUNITY_INDEX_SYNC_H
#define RAGREACTOR_COMMUNITY_INDEX_SYNC_H

#include "../ai_rag/embedding_provider.h"

#include <mysql/mysql.h>
#include <cstddef>
#include <string>

struct CommunitySyncStats
{
    std::size_t indexed = 0;
    std::size_t removed = 0;
    std::size_t failed = 0;
    std::size_t unchanged = 0;
};

class CommunityIndexSync
{
public:
    CommunityIndexSync(MYSQL *connection, const rag::EmbeddingProvider &provider,
                       std::string vector_path, std::string hnsw_path);

    bool run(CommunitySyncStats &stats, std::string *error = nullptr) const;

private:
    MYSQL *connection_;
    const rag::EmbeddingProvider &provider_;
    std::string vector_path_;
    std::string hnsw_path_;
};

#endif

