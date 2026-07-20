#include "../ai_rag/bailian_embedding_provider.h"
#include "../ai_rag/embedding_provider.h"
#include "../community/community_index_sync.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
std::string env(const char *name, const std::string &fallback = "")
{
    const char *value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

std::size_t env_size(const char *name, std::size_t fallback)
{
    try { return static_cast<std::size_t>(std::stoull(env(name))); }
    catch (...) { return fallback; }
}
}

int main()
{
    MYSQL *mysql = mysql_init(nullptr);
    if (!mysql || !mysql_real_connect(mysql, "127.0.0.1", env("MYSQL_USER").c_str(),
        env("MYSQL_PASSWORD").c_str(), env("MYSQL_DATABASE").c_str(), 3306, nullptr, 0))
    {
        std::cerr << "community index: database connection failed\n";
        if (mysql) mysql_close(mysql);
        return 1;
    }

    try
    {
        std::unique_ptr<rag::EmbeddingProvider> provider;
        if (env("COMMUNITY_INDEX_PROVIDER") == "mock")
            provider = std::make_unique<rag::MockEmbeddingProvider>(
                env_size("RAG_EMBEDDING_DIMENSION", 256));
        else
            provider = std::make_unique<rag::BailianEmbeddingProvider>(
                env("BAILIAN_BASE_URL"), env("BAILIAN_API_KEY"),
                env("RAG_EMBEDDING_MODEL", "text-embedding-v4"),
                env_size("RAG_EMBEDDING_DIMENSION", 1024),
                env_size("RAG_CONNECT_TIMEOUT_MS", 3000),
                env_size("RAG_EMBEDDING_TIMEOUT_MS", 10000));
        CommunityIndexSync sync(mysql, *provider,
            env("COMMUNITY_INDEX_PATH", "knowledge/index/community-bailian-v4-1024.ragvec"),
            env("COMMUNITY_HNSW_INDEX_PATH", "knowledge/index/community-bailian-v4-1024.hnsw"));
        CommunitySyncStats stats;
        std::string error;
        if (!sync.run(stats, &error))
        {
            std::cerr << "community index failed: " << error << '\n';
            mysql_close(mysql);
            return 1;
        }
        std::cout << "community index complete: indexed=" << stats.indexed
                  << " removed=" << stats.removed << " failed=" << stats.failed
                  << " unchanged=" << stats.unchanged << '\n';
    }
    catch (const std::exception &exception)
    {
        std::cerr << "community index failed: " << exception.what() << '\n';
        mysql_close(mysql);
        return 1;
    }
    mysql_close(mysql);
    return 0;
}
