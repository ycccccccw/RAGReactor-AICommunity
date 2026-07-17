#ifndef RAGREACTOR_HTTP_JSON_CLIENT_H
#define RAGREACTOR_HTTP_JSON_CLIENT_H

#include <boost/json/value.hpp>

#include <string>

namespace rag
{
class HttpJsonClient
{
public:
    HttpJsonClient(long connect_timeout_ms = 3000, long request_timeout_ms = 30000);

    boost::json::value post(const std::string &url, const std::string &api_key,
                            const boost::json::value &body) const;

private:
    long connect_timeout_ms_;
    long request_timeout_ms_;
};
}

#endif
