#ifndef RAGREACTOR_MYSQL_COMMUNITY_STORE_H
#define RAGREACTOR_MYSQL_COMMUNITY_STORE_H

#include "../api/community_store.h"

#include <mysql/mysql.h>

class MysqlCommunityStore : public CommunityStore
{
public:
    explicit MysqlCommunityStore(MYSQL *connection) : connection_(connection) {}

    bool fetch_feed(const std::string &username,
                    const CommunityFeedQuery &query,
                    CommunityFeedPage &page,
                    std::string &error) override;

    bool record_action(const std::string &username,
                       const CommunityAction &action,
                       CommunityActionResult &result,
                       std::string &error) override;

    bool fetch_post(unsigned long long post_id, CommunityPost &post,
                    std::string &error) override;

private:
    MYSQL *connection_;
};

#endif
