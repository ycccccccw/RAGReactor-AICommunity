#include "../community/mysql_community_store.h"

#include <cstdlib>
#include <iostream>

namespace
{
std::string env(const char *name)
{
    const char *value = std::getenv(name);
    return value ? value : "";
}
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <username>\n";
        return 2;
    }
    MYSQL *mysql = mysql_init(nullptr);
    if (!mysql || !mysql_real_connect(mysql, "127.0.0.1", env("MYSQL_USER").c_str(),
        env("MYSQL_PASSWORD").c_str(), env("MYSQL_DATABASE").c_str(), 3306, nullptr, 0))
    {
        std::cerr << "recommendation preview: database connection failed\n";
        if (mysql) mysql_close(mysql);
        return 1;
    }
    MysqlCommunityStore store(mysql);
    CommunityFeedQuery query;
    query.personalized = true;
    query.limit = 10;
    CommunityFeedPage page;
    std::string error;
    if (!store.fetch_feed(argv[1], query, page, error))
    {
        std::cerr << "recommendation preview failed: " << error << '\n';
        mysql_close(mysql);
        return 1;
    }
    std::cout << "personalized=" << (page.semantic_profile ? "true" : "false")
              << " fallback=" << (page.fallback_reason.empty() ? "none" : page.fallback_reason)
              << " next_cursor=" << (page.next_cursor.empty() ? "none" : page.next_cursor) << '\n';
    for (std::size_t index = 0; index < page.posts.size(); ++index)
        std::cout << index + 1 << " post=" << page.posts[index].id
                  << " score=" << page.posts[index].recommendation_score
                  << " source=" << page.posts[index].recommendation_source
                  << " author=" << page.posts[index].username << '\n';
    mysql_close(mysql);
    return 0;
}

